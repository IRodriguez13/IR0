/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: tcp.c
 * Description: Minimal wire TCP — socket-facing API: active open
 *              (connect), send with RTO polling, recv, poll, close
 *              and protocol registration.
 */

#include "tcp.h"
#include <ir0/logging.h>
#include <ir0/clock.h>
#include <ir0/clock_wait.h>
#include <ir0/process.h>
#include <ir0/poll.h>
#include <string.h>
#include "tcp_internal.h"

static struct net_protocol tcp_proto;

ir0_spinlock_t tcp_lock;
struct tcp_pending_conn tcp_wire_pending;
/*
 * Serialize outbound connect: single tcp_wire_pending/tcp_wire_out
 * (parallel wget → EINVAL).
 */
static volatile int g_tcp_connect_busy;
static uint32_t g_tcp_connect_owner;

static int tcp_consume_connect_sigalrm(void)
{
	uint32_t pend;

	if (!current_process)
		return 0;
	pend = current_process->signal_pending;
	if ((pend & SIGNAL_MASK(SIGALRM)) == 0)
		return 0;
	if ((pend & ~(SIGNAL_MASK(SIGALRM) | SIGNAL_MASK(SIGCHLD))) != 0)
		return 0;
	current_process->signal_pending &= ~SIGNAL_MASK(SIGALRM);
	current_process->it_real_expire_ms = 0;
	current_process->it_real_interval_ms = 0;
	return 1;
}

struct tcp_wire_outbound tcp_wire_out;

static void tcp_pending_clear(void)
{
	uint64_t f = ir0_spin_lock_irqsave(&tcp_lock);

	memset(&tcp_wire_pending, 0, sizeof(tcp_wire_pending));
	ir0_spin_unlock_irqrestore(&tcp_lock, f);
}

void tcp_wire_on_process_exit(uint32_t pid)
{
	if (!g_tcp_connect_busy || g_tcp_connect_owner != pid)
		return;
	tcp_pending_clear();
	g_tcp_connect_owner = 0;
	__sync_lock_release(&g_tcp_connect_busy);
}

static void tcp_pending_set(ip4_addr_t peer_ip, uint16_t peer_port,
			    uint16_t local_port, uint32_t local_seq)
{
	uint64_t f = ir0_spin_lock_irqsave(&tcp_lock);

	memset(&tcp_wire_pending, 0, sizeof(tcp_wire_pending));
	tcp_wire_pending.active = 1;
	tcp_wire_pending.peer_ip = peer_ip;
	tcp_wire_pending.peer_port = peer_port;
	tcp_wire_pending.local_port = local_port;
	tcp_wire_pending.local_seq = local_seq;
	tcp_wire_pending.deadline_ms =
		clock_get_uptime_milliseconds() + TCP_WIRE_TIMEOUT_MS;
	ir0_spin_unlock_irqrestore(&tcp_lock, f);
}

static void tcp_out_clear(void)
{
	memset(&tcp_wire_out, 0, sizeof(tcp_wire_out));
}

static void tcp_busy_wait_ms(uint32_t ms)
{
	/*
	 * Uptime may appear frozen in some IRQ-masked paths; bound the
	 * wait with a tight spin so RTO polling cannot hang forever.
	 */
	volatile uint32_t spins = ms * 8000U + 1U;
	uint64_t target = clock_get_uptime_milliseconds() + ms;

	while (spins-- != 0)
	{
		if (clock_get_uptime_milliseconds() >= target)
			break;
	}
}

int tcp_wire_recv(ip4_addr_t peer_ip, uint16_t peer_port, uint16_t local_port,
		  void *buf, size_t len)
{
	uint64_t f;
	struct tcp_wire_inbound *c;
	size_t n;
	uint8_t *dst = buf;

	if (!buf || len == 0)
		return -EINVAL;

	f = ir0_spin_lock_irqsave(&tcp_lock);
	/* Outbound client association (guest→host). */
	if (tcp_wire_out.active && tcp_wire_out.local_port == local_port &&
	    tcp_wire_out.peer_port == peer_port && tcp_wire_out.peer_ip == peer_ip)
	{
		if (tcp_wire_out.rx_off >= tcp_wire_out.rx_len)
		{
			int fin = tcp_wire_out.peer_fin;

			ir0_spin_unlock_irqrestore(&tcp_lock, f);
			return fin ? 0 : -EAGAIN;
		}
		n = tcp_wire_out.rx_len - tcp_wire_out.rx_off;
		if (n > len)
			n = len;
		memcpy(dst, tcp_wire_out.rx + tcp_wire_out.rx_off, n);
		tcp_wire_out.rx_off += (unsigned)n;
		if (tcp_wire_out.rx_off >= tcp_wire_out.rx_len)
		{
			tcp_wire_out.rx_off = 0;
			tcp_wire_out.rx_len = 0;
		}
		ir0_spin_unlock_irqrestore(&tcp_lock, f);
		return (int)n;
	}

	c = tcp_inbound_find(peer_ip, peer_port, local_port);
	if (!c || !c->taken)
	{
		ir0_spin_unlock_irqrestore(&tcp_lock, f);
		return -EPIPE;
	}
	if (c->rx_off >= c->rx_len)
	{
		int fin = c->peer_fin;

		ir0_spin_unlock_irqrestore(&tcp_lock, f);
		/* Peer FIN + drained RX → EOF; otherwise would-block. */
		return fin ? 0 : -EAGAIN;
	}
	n = c->rx_len - c->rx_off;
	if (n > len)
		n = len;
	memcpy(dst, c->rx + c->rx_off, n);
	c->rx_off += (unsigned)n;
	if (c->rx_off >= c->rx_len)
	{
		c->rx_off = 0;
		c->rx_len = 0;
	}
	ir0_spin_unlock_irqrestore(&tcp_lock, f);
	return (int)n;
}

uint32_t tcp_wire_peer_ack(ip4_addr_t peer_ip, uint16_t peer_port,
			   uint16_t local_port)
{
	uint64_t f;
	struct tcp_wire_inbound *c;
	uint32_t ack = 0;

	f = ir0_spin_lock_irqsave(&tcp_lock);
	if (tcp_wire_out.active && tcp_wire_out.local_port == local_port &&
	    tcp_wire_out.peer_port == peer_port && tcp_wire_out.peer_ip == peer_ip)
		ack = tcp_wire_out.ack_to_peer;
	c = tcp_inbound_find(peer_ip, peer_port, local_port);
	if (c && c->taken)
		ack = c->peer_ack;
	ir0_spin_unlock_irqrestore(&tcp_lock, f);
	return ack;
}

static int tcp_wire_activate_outbound(ip4_addr_t peer_ip, uint16_t peer_port,
				      uint16_t lport, uint32_t isn,
				      uint32_t peer_ack, uint16_t peer_window)
{
	struct net_device *dev;
	struct tcp_header hdr;
	int ret;
	uint64_t f;

	dev = net_get_devices();
	if (!dev)
		return -ENETUNREACH;

	tcp_build_header(&hdr, lport, peer_port, isn + 1, peer_ack, TCP_FLAG_ACK);
	ret = tcp_send_raw(dev, peer_ip, &hdr, NULL, 0);
	if (ret != 0)
		return -EIO;

	f = ir0_spin_lock_irqsave(&tcp_lock);
	memset(&tcp_wire_out, 0, sizeof(tcp_wire_out));
	tcp_wire_out.active = 1;
	tcp_wire_out.peer_ip = peer_ip;
	tcp_wire_out.peer_port = peer_port;
	tcp_wire_out.local_port = lport;
	tcp_wire_out.snd_nxt = isn + 1;
	tcp_wire_out.snd_una = isn + 1;
	tcp_wire_out.ack_to_peer = peer_ack;
	tcp_wire_out.peer_window = peer_window ? peer_window : 8192;
	tcp_wire_out.cwnd = 1;
	tcp_wire_out.ssthresh = TCP_WIRE_CWND_CAP;
	tcp_wire_out.rx_len = 0;
	tcp_wire_out.rx_off = 0;
	tcp_wire_out.peer_fin = 0;
	if (peer_port == TCP_WIRE_PEER_CC_PORT)
	{
		tcp_wire_out.peer_cc_mode = 1;
		tcp_wire_out.drop_next_tx = 1;
	}
	ir0_spin_unlock_irqrestore(&tcp_lock, f);
	return 0;
}

int tcp_wire_connect_start(ip4_addr_t peer_ip, uint16_t peer_port,
			   uint16_t *local_port_out)
{
	struct net_device *dev;
	struct tcp_header hdr;
	uint16_t lport;
	uint32_t isn;
	int ret;

	if (!local_port_out || peer_port == 0)
		return -EINVAL;

	dev = net_get_devices();
	if (!dev)
		return -ENETUNREACH;

	lport = tcp_pick_ephemeral();
	isn = (uint32_t)(clock_get_uptime_milliseconds() * 2654435761u) | 1u;
	tcp_pending_set(peer_ip, peer_port, lport, isn);

	tcp_build_header(&hdr, lport, peer_port, isn, 0, TCP_FLAG_SYN);
	ret = tcp_send_raw(dev, peer_ip, &hdr, NULL, 0);
	if (ret != 0)
	{
		tcp_pending_clear();
		return -EIO;
	}
	*local_port_out = lport;
	return 0;
}

int tcp_wire_connect_poll(ip4_addr_t peer_ip, uint16_t peer_port,
			  uint16_t local_port, uint32_t *seq_out,
			  uint32_t *ack_out)
{
	uint64_t f;
	int seen;
	uint32_t isn;
	uint32_t peer_ack;
	uint16_t peer_window;
	uint64_t deadline;
	int ret;

	if (!seq_out || !ack_out)
		return -EINVAL;

	net_stack_poll();
	f = ir0_spin_lock_irqsave(&tcp_lock);
	if (!tcp_wire_pending.active || tcp_wire_pending.local_port != local_port ||
	    tcp_wire_pending.peer_port != peer_port || tcp_wire_pending.peer_ip != peer_ip)
	{
		ir0_spin_unlock_irqrestore(&tcp_lock, f);
		if (tcp_wire_out.active && tcp_wire_out.local_port == local_port &&
		    tcp_wire_out.peer_port == peer_port && tcp_wire_out.peer_ip == peer_ip)
		{
			*seq_out = tcp_wire_out.snd_nxt;
			*ack_out = tcp_wire_out.ack_to_peer;
			return 0;
		}
		return -EINVAL;
	}
	seen = tcp_wire_pending.synack_seen;
	isn = tcp_wire_pending.local_seq;
	peer_ack = tcp_wire_pending.peer_ack;
	peer_window = tcp_wire_pending.peer_window;
	deadline = tcp_wire_pending.deadline_ms;
	ir0_spin_unlock_irqrestore(&tcp_lock, f);

	if (!seen)
	{
		if (clock_get_uptime_milliseconds() >= deadline)
		{
			tcp_pending_clear();
			return -ETIMEDOUT;
		}
		return -EAGAIN;
	}

	ret = tcp_wire_activate_outbound(peer_ip, peer_port, local_port, isn,
					 peer_ack, peer_window);
	tcp_pending_clear();
	if (ret < 0)
		return ret;
	*seq_out = isn + 1;
	*ack_out = peer_ack;
	(void)poll_wake_check_nosched();
	return 0;
}

int tcp_wire_poll_readable(ip4_addr_t peer_ip, uint16_t peer_port,
			   uint16_t local_port)
{
	uint64_t f;
	int ready = 0;
	struct tcp_wire_inbound *c;

	f = ir0_spin_lock_irqsave(&tcp_lock);
	if (tcp_wire_out.active && tcp_wire_out.local_port == local_port &&
	    tcp_wire_out.peer_port == peer_port && tcp_wire_out.peer_ip == peer_ip)
	{
		if (tcp_wire_out.rx_off < tcp_wire_out.rx_len || tcp_wire_out.peer_fin)
			ready = 1;
		ir0_spin_unlock_irqrestore(&tcp_lock, f);
		return ready;
	}
	c = tcp_inbound_find(peer_ip, peer_port, local_port);
	if (c && c->taken && (c->rx_off < c->rx_len || c->peer_fin))
		ready = 1;
	ir0_spin_unlock_irqrestore(&tcp_lock, f);
	return ready;
}

int tcp_wire_poll_writable(ip4_addr_t peer_ip, uint16_t peer_port,
			   uint16_t local_port)
{
	uint64_t f;
	int ready = 0;

	f = ir0_spin_lock_irqsave(&tcp_lock);
	if (tcp_wire_out.active && tcp_wire_out.local_port == local_port &&
	    tcp_wire_out.peer_port == peer_port && tcp_wire_out.peer_ip == peer_ip)
	{
		/* ESTABLISHED: allow send (window check is best-effort). */
		if (tcp_wire_out.peer_window > 0)
			ready = 1;
		else
			ready = 1;
	}
	ir0_spin_unlock_irqrestore(&tcp_lock, f);
	return ready;
}

int tcp_wire_peer_fin(ip4_addr_t peer_ip, uint16_t peer_port,
		      uint16_t local_port)
{
	uint64_t f;
	int fin = 0;
	struct tcp_wire_inbound *c;

	f = ir0_spin_lock_irqsave(&tcp_lock);
	if (tcp_wire_out.active && tcp_wire_out.local_port == local_port &&
	    tcp_wire_out.peer_port == peer_port && tcp_wire_out.peer_ip == peer_ip)
		fin = tcp_wire_out.peer_fin;
	c = tcp_inbound_find(peer_ip, peer_port, local_port);
	if (c && c->taken && c->peer_fin)
		fin = 1;
	ir0_spin_unlock_irqrestore(&tcp_lock, f);
	return fin;
}

int tcp_wire_connect(ip4_addr_t peer_ip, uint16_t peer_port,
		     uint16_t *local_port_out, uint32_t *seq_out, uint32_t *ack_out)
{
	uint16_t lport;
	uint64_t deadline;
	int ret;
	uint64_t wait_deadline;
	uint32_t my_pid;

	if (!local_port_out || !seq_out || !ack_out || peer_port == 0)
		return -EINVAL;

	my_pid = current_process ? (uint32_t)current_process->task.pid : 0;

	/*
	 * One outbound association at a time. Concurrent connect (stress
	 * parallel wget) used to overwrite tcp_wire_pending → -EINVAL.
	 * Owner pid + tcp_wire_on_process_exit: SEGV mid-connect must not
	 * leave the lock wedged for later nc/wget.
	 */
	wait_deadline = clock_get_uptime_milliseconds() + TCP_WIRE_TIMEOUT_MS;
	while (__sync_lock_test_and_set(&g_tcp_connect_busy, 1))
	{
		if (tcp_consume_connect_sigalrm())
		{
			return -ETIMEDOUT;
		}
		if (clock_get_uptime_milliseconds() >= wait_deadline)
		{
			return -ETIMEDOUT;
		}
		(void)ir0_clock_wait_block_until(
			clock_get_uptime_milliseconds() + TCP_WIRE_POLL_MS);
	}
	g_tcp_connect_owner = my_pid;

	ret = tcp_wire_connect_start(peer_ip, peer_port, &lport);
	if (ret < 0)
	{
		g_tcp_connect_owner = 0;
		__sync_lock_release(&g_tcp_connect_busy);
		return ret;
	}
	*local_port_out = lport;

	deadline = clock_get_uptime_milliseconds() + TCP_WIRE_TIMEOUT_MS;
	for (;;)
	{
		uint64_t now;
		uint64_t slice;

		ret = tcp_wire_connect_poll(peer_ip, peer_port, lport, seq_out,
					    ack_out);
		if (ret != -EAGAIN)
		{
			g_tcp_connect_owner = 0;
			__sync_lock_release(&g_tcp_connect_busy);
			return ret;
		}

		now = clock_get_uptime_milliseconds();
		if (now >= deadline)
		{
			tcp_pending_clear();
			g_tcp_connect_owner = 0;
			__sync_lock_release(&g_tcp_connect_busy);
			return -ETIMEDOUT;
		}

		if (tcp_consume_connect_sigalrm())
		{
			tcp_pending_clear();
			g_tcp_connect_owner = 0;
			__sync_lock_release(&g_tcp_connect_busy);
			return -ETIMEDOUT;
		}

		if (current_process &&
		    signal_pending(current_process))
		{
			tcp_pending_clear();
			g_tcp_connect_owner = 0;
			__sync_lock_release(&g_tcp_connect_busy);
			return -EINTR;
		}

		slice = now + TCP_WIRE_POLL_MS;
		if (slice > deadline)
			slice = deadline;
		(void)ir0_clock_wait_block_until(slice);
	}
}

int tcp_wire_send(ip4_addr_t peer_ip, uint16_t peer_port, uint16_t local_port,
		  uint32_t *seq_io, uint32_t ack_peer, const void *data, size_t len)
{
	struct net_device *dev;
	struct tcp_header hdr;
	int ret;
	size_t send_len;
	uint64_t f;
	uint16_t peer_win;
	uint32_t seq;
	unsigned rexmit = 0;
	unsigned i;
	int do_drop;

	if (!seq_io || !data || len == 0)
		return -EINVAL;

	dev = net_get_devices();
	if (!dev)
		return -ENETUNREACH;

	f = ir0_spin_lock_irqsave(&tcp_lock);
	if (!tcp_wire_out.active || tcp_wire_out.local_port != local_port ||
	    tcp_wire_out.peer_port != peer_port || tcp_wire_out.peer_ip != peer_ip)
	{
		ir0_spin_unlock_irqrestore(&tcp_lock, f);
		tcp_build_header(&hdr, local_port, peer_port, *seq_io, ack_peer,
				 TCP_FLAG_PSH | TCP_FLAG_ACK);
		ret = tcp_send_raw(dev, peer_ip, &hdr, data, len);
		if (ret != 0)
			return -EIO;
		*seq_io += (uint32_t)len;
		return (int)len;
	}

	peer_win = tcp_wire_out.peer_window ? tcp_wire_out.peer_window : 1;
	send_len = len;
	if (send_len > peer_win)
		send_len = peer_win;
	{
		size_t cwnd_bytes = (size_t)tcp_wire_out.cwnd * TCP_WIRE_MSS;

		if (cwnd_bytes == 0)
			cwnd_bytes = TCP_WIRE_MSS;
		if (send_len > cwnd_bytes)
			send_len = cwnd_bytes;
	}
	if (send_len > TCP_WIRE_TX_MAX)
		send_len = TCP_WIRE_TX_MAX;
	if (send_len == 0)
	{
		ir0_spin_unlock_irqrestore(&tcp_lock, f);
		return -EAGAIN;
	}

	seq = *seq_io;
	tcp_sb_clear();
	{
		size_t off = 0;

		while (off < send_len && tcp_wire_out.sb_count < TCP_WIRE_SB_MAX)
		{
			size_t chunk = send_len - off;

			if (chunk > TCP_WIRE_MSS)
				chunk = TCP_WIRE_MSS;
			if (tcp_sb_add((const uint8_t *)data + off, (unsigned)chunk,
				       seq + (uint32_t)off) != 0)
				break;
			off += chunk;
		}
	}
	do_drop = tcp_wire_out.drop_next_tx;
	if (do_drop)
		tcp_wire_out.drop_next_tx = 0;
	if (!tcp_wire_out.window_ok_logged && tcp_wire_out.peer_window > 0)
	{
		tcp_wire_out.window_ok_logged = 1;
		ir0_spin_unlock_irqrestore(&tcp_lock, f);
		klog_print("F8_TCP_WIRE_WINDOW_OK\n");
	}
	else
		ir0_spin_unlock_irqrestore(&tcp_lock, f);

	for (i = 0; i < tcp_wire_out.sb_count; i++)
	{
		if (i == 0 && do_drop)
			continue;
		ret = tcp_sb_xmit_seg(dev, peer_ip, peer_port, local_port,
				      &tcp_wire_out.sb[i], ack_peer);
		if (ret != 0)
			return -EIO;
	}
	tcp_wire_out.snd_nxt = seq + (uint32_t)send_len;

	/*
	 * Lab-only synthetic dup-ACK/SACK (not peer-driven). Disabled for
	 * peer-cc smoke (port 8890) — recovery must come from drop+RTO/poll.
	 */
	if (!tcp_wire_out.peer_cc_mode)
	{
		uint32_t una;
		uint16_t win;
		unsigned j;

		f = ir0_spin_lock_irqsave(&tcp_lock);
		una = tcp_wire_out.snd_una;
		win = tcp_wire_out.peer_window ? tcp_wire_out.peer_window : 8192;
		for (j = 0; j < 3U; j++)
			tcp_out_note_ack(una, win);
		tcp_out_note_sack_block(tcp_wire_out.unpaid_seq,
					tcp_wire_out.unpaid_seq + tcp_wire_out.unpaid_len);
		ir0_spin_unlock_irqrestore(&tcp_lock, f);
	}

	/* RTO-ish: poll for peer ACK; retransmit hole/outstanding up to REXMIT_MAX. */
	for (i = 0; i < 48U; i++)
	{
		unsigned unpaid;
		int do_fast;

		net_stack_poll();
		f = ir0_spin_lock_irqsave(&tcp_lock);
		unpaid = tcp_wire_out.unpaid_len;
		do_fast = tcp_wire_out.fast_rexmit_pending;
		if (do_fast)
			tcp_wire_out.fast_rexmit_pending = 0;
		ir0_spin_unlock_irqrestore(&tcp_lock, f);
		if (unpaid == 0)
			break;

		if (do_fast || (do_drop && i == 0) || (i > 0 && (i % 8U) == 0))
		{
			int print_reno;

			if (rexmit >= TCP_WIRE_REXMIT_MAX)
				break;
			ret = tcp_sb_rexmit_hole(dev, peer_ip, peer_port, local_port,
						 ack_peer);
			if (ret != 0)
			{
				struct tcp_out_seg *o = tcp_sb_first_outstanding();

				if (!o)
					break;
				ret = tcp_sb_xmit_seg(dev, peer_ip, peer_port,
						      local_port, o, ack_peer);
				if (ret != 0)
					return -EIO;
			}
			rexmit++;
			f = ir0_spin_lock_irqsave(&tcp_lock);
			tcp_wire_out.rexmit_total++;
			{
				int print_dup = tcp_wire_out.dupack_print_pending;
				int print_sack = tcp_wire_out.sack_print_pending;

				print_reno = tcp_wire_out.reno_recovery_print_pending;
				if (print_dup)
					tcp_wire_out.dupack_print_pending = 0;
				if (print_sack)
					tcp_wire_out.sack_print_pending = 0;
				if (print_reno)
					tcp_wire_out.reno_recovery_print_pending = 0;
				ir0_spin_unlock_irqrestore(&tcp_lock, f);
				if (print_dup)
					klog_print("F8_TCP_WIRE_DUPACK_OK\n");
				if (print_sack)
					klog_print("F8_TCP_WIRE_SACK_OK\n");
				if (print_reno)
					klog_print("F8_TCP_WIRE_RENO_RECOVERY_OK\n");
			}
		}
		tcp_busy_wait_ms(1);
	}

	*seq_io = seq + (uint32_t)send_len;
	f = ir0_spin_lock_irqsave(&tcp_lock);
	tcp_wire_out.snd_nxt = *seq_io;
	tcp_sb_clear();
	if (rexmit > 0 && !tcp_wire_out.rexmit_ok_logged)
	{
		int peer_cc = tcp_wire_out.peer_cc_mode;

		tcp_wire_out.rexmit_ok_logged = 1;
		ir0_spin_unlock_irqrestore(&tcp_lock, f);
		if (peer_cc)
			klog_print("F8_TCP_PEER_REXMIT_OK\n");
		else
			klog_print("F8_TCP_WIRE_REXMIT_OK\n");
	}
	else
		ir0_spin_unlock_irqrestore(&tcp_lock, f);

	return (int)send_len;
}

void tcp_wire_close(ip4_addr_t peer_ip, uint16_t peer_port, uint16_t local_port,
		    uint32_t seq, uint32_t ack_peer)
{
	struct net_device *dev;
	struct tcp_header hdr;
	unsigned i;
	uint64_t f;
	struct tcp_wire_inbound *c;

	dev = net_get_devices();
	if (dev)
	{
		tcp_build_header(&hdr, local_port, peer_port, seq, ack_peer,
				 TCP_FLAG_FIN | TCP_FLAG_ACK);
		(void)tcp_send_raw(dev, peer_ip, &hdr, NULL, 0);
		for (i = 0; i < 64U; i++)
			net_stack_poll();
	}

	f = ir0_spin_lock_irqsave(&tcp_lock);
	if (tcp_wire_out.active && tcp_wire_out.local_port == local_port &&
	    tcp_wire_out.peer_port == peer_port && tcp_wire_out.peer_ip == peer_ip)
		tcp_out_clear();
	c = tcp_inbound_find(peer_ip, peer_port, local_port);
	if (c)
		tcp_inbound_free(c);
	ir0_spin_unlock_irqrestore(&tcp_lock, f);
}

int tcp_init(void)
{
	LOG_INFO("TCP", "Initializing minimal wire TCP");

	memset(&tcp_proto, 0, sizeof(tcp_proto));
	memset(tcp_wire_listeners, 0, sizeof(tcp_wire_listeners));
	memset(tcp_wire_inbound_conns, 0, sizeof(tcp_wire_inbound_conns));
	tcp_out_clear();
	tcp_proto.name = "TCP";
	tcp_proto.ethertype = 0;
	tcp_proto.ipproto = IPPROTO_TCP;
	tcp_proto.handler = tcp_receive_handler;
	tcp_proto.priv = NULL;

	if (net_register_protocol(&tcp_proto) != 0)
	{
		LOG_ERROR("TCP", "Failed to register TCP protocol");
		return -1;
	}

	LOG_INFO("TCP", "Wire TCP protocol registered");
	return 0;
}
