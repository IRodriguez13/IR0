/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: tcp_listen.c
 * Description: Wire TCP passive open — listeners, inbound
 *              connections, SYN/SYN-ACK and inbound data.
 */

#include "tcp.h"
#include <ir0/clock.h>
#include <ir0/process.h>
#include <string.h>
#include "tcp_internal.h"

struct tcp_wire_listener tcp_wire_listeners[TCP_WIRE_LISTEN_MAX];
struct tcp_wire_inbound tcp_wire_inbound_conns[TCP_WIRE_CONN_MAX];

static int tcp_listener_find(uint16_t port)
{
	int i;

	for (i = 0; i < TCP_WIRE_LISTEN_MAX; i++)
	{
		if (tcp_wire_listeners[i].active && tcp_wire_listeners[i].port == port)
			return i;
	}
	return -1;
}

struct tcp_wire_inbound *tcp_inbound_find(ip4_addr_t peer_ip,
					  uint16_t peer_port,
					  uint16_t local_port)
{
	int i;

	for (i = 0; i < TCP_WIRE_CONN_MAX; i++)
	{
		if (tcp_wire_inbound_conns[i].active &&
		    tcp_wire_inbound_conns[i].local_port == local_port &&
		    tcp_wire_inbound_conns[i].peer_port == peer_port &&
		    tcp_wire_inbound_conns[i].peer_ip == peer_ip)
			return &tcp_wire_inbound_conns[i];
	}
	return NULL;
}

static struct tcp_wire_inbound *tcp_inbound_alloc(void)
{
	int i;

	for (i = 0; i < TCP_WIRE_CONN_MAX; i++)
	{
		if (!tcp_wire_inbound_conns[i].active)
		{
			memset(&tcp_wire_inbound_conns[i], 0, sizeof(tcp_wire_inbound_conns[i]));
			tcp_wire_inbound_conns[i].active = 1;
			return &tcp_wire_inbound_conns[i];
		}
	}
	return NULL;
}

void tcp_inbound_free(struct tcp_wire_inbound *c)
{
	if (c)
		memset(c, 0, sizeof(*c));
}

static void tcp_rx_append(struct tcp_wire_inbound *c, const uint8_t *data,
			  size_t len)
{
	size_t space;
	size_t n;

	if (!c || !data || len == 0)
		return;
	if (c->rx_off > 0 && c->rx_off == c->rx_len)
	{
		c->rx_off = 0;
		c->rx_len = 0;
	}
	if (c->rx_off > 0)
	{
		memmove(c->rx, c->rx + c->rx_off, c->rx_len - c->rx_off);
		c->rx_len -= c->rx_off;
		c->rx_off = 0;
	}
	space = TCP_WIRE_RX_MAX - c->rx_len;
	n = len;
	if (n > space)
		n = space;
	if (n == 0)
		return;
	memcpy(c->rx + c->rx_len, data, n);
	c->rx_len += (unsigned)n;
}

int tcp_wire_listen_register(uint16_t port)
{
	uint64_t f;
	int i;

	if (port == 0)
		return -EINVAL;

	f = ir0_spin_lock_irqsave(&tcp_lock);
	if (tcp_listener_find(port) >= 0)
	{
		ir0_spin_unlock_irqrestore(&tcp_lock, f);
		return 0;
	}
	for (i = 0; i < TCP_WIRE_LISTEN_MAX; i++)
	{
		if (!tcp_wire_listeners[i].active)
		{
			tcp_wire_listeners[i].active = 1;
			tcp_wire_listeners[i].port = port;
			ir0_spin_unlock_irqrestore(&tcp_lock, f);
			return 0;
		}
	}
	ir0_spin_unlock_irqrestore(&tcp_lock, f);
	return -ENOMEM;
}

void tcp_wire_listen_unregister(uint16_t port)
{
	uint64_t f;
	int i;

	f = ir0_spin_lock_irqsave(&tcp_lock);
	for (i = 0; i < TCP_WIRE_LISTEN_MAX; i++)
	{
		if (tcp_wire_listeners[i].active && tcp_wire_listeners[i].port == port)
			tcp_wire_listeners[i].active = 0;
	}
	for (i = 0; i < TCP_WIRE_CONN_MAX; i++)
	{
		if (tcp_wire_inbound_conns[i].active && tcp_wire_inbound_conns[i].local_port == port &&
		    !tcp_wire_inbound_conns[i].taken)
			tcp_inbound_free(&tcp_wire_inbound_conns[i]);
	}
	ir0_spin_unlock_irqrestore(&tcp_lock, f);
}

int tcp_wire_accept_take(uint16_t listen_port, ip4_addr_t *peer_ip,
			 uint16_t *peer_port, uint16_t *local_port,
			 uint32_t *seq_out, uint32_t *ack_out)
{
	uint64_t f;
	int i;

	if (!peer_ip || !peer_port || !local_port || !seq_out || !ack_out)
		return -EINVAL;

	f = ir0_spin_lock_irqsave(&tcp_lock);
	for (i = 0; i < TCP_WIRE_CONN_MAX; i++)
	{
		struct tcp_wire_inbound *c = &tcp_wire_inbound_conns[i];

		if (c->active && c->established && !c->taken &&
		    c->local_port == listen_port)
		{
			c->taken = 1;
			*peer_ip = c->peer_ip;
			*peer_port = c->peer_port;
			*local_port = c->local_port;
			*seq_out = c->local_seq;
			*ack_out = c->peer_ack;
			ir0_spin_unlock_irqrestore(&tcp_lock, f);
			return 0;
		}
	}
	ir0_spin_unlock_irqrestore(&tcp_lock, f);
	return -EAGAIN;
}

void tcp_handle_listen_syn(struct net_device *dev, ip4_addr_t src_ip,
			   uint16_t src_port, uint16_t dest_port,
			   uint32_t seq)
{
	struct tcp_wire_inbound *c;
	struct tcp_header hdr;
	uint32_t isn;
	uint64_t f;

	f = ir0_spin_lock_irqsave(&tcp_lock);
	if (tcp_listener_find(dest_port) < 0)
	{
		ir0_spin_unlock_irqrestore(&tcp_lock, f);
		return;
	}
	c = tcp_inbound_find(src_ip, src_port, dest_port);
	if (c)
	{
		/*
		 * Retransmitted SYN (lost SYN-ACK / RTL8139 TX stall): resend
		 * SYN-ACK for the half-open slot instead of ignoring.
		 */
		if (!c->established)
		{
			uint32_t syn_seq = c->local_seq - 1;

			c->peer_ack = seq + 1;
			ir0_spin_unlock_irqrestore(&tcp_lock, f);
			tcp_build_header(&hdr, dest_port, src_port, syn_seq,
					 seq + 1, TCP_FLAG_SYN | TCP_FLAG_ACK);
			(void)tcp_send_raw(dev, src_ip, &hdr, NULL, 0);
			return;
		}
		ir0_spin_unlock_irqrestore(&tcp_lock, f);
		return;
	}
	c = tcp_inbound_alloc();
	if (!c)
	{
		ir0_spin_unlock_irqrestore(&tcp_lock, f);
		return;
	}
	isn = (uint32_t)(clock_get_uptime_milliseconds() * 2654435761u) | 1u;
	c->local_port = dest_port;
	c->peer_ip = src_ip;
	c->peer_port = src_port;
	c->local_seq = isn + 1;
	c->peer_ack = seq + 1;
	c->established = 0;
	c->taken = 0;
	c->peer_fin = 0;
	ir0_spin_unlock_irqrestore(&tcp_lock, f);

	tcp_build_header(&hdr, dest_port, src_port, isn, seq + 1,
			 TCP_FLAG_SYN | TCP_FLAG_ACK);
	(void)tcp_send_raw(dev, src_ip, &hdr, NULL, 0);
}

void tcp_handle_inbound(struct net_device *dev, ip4_addr_t src_ip,
			uint16_t src_port, uint16_t dest_port,
			uint32_t seq, uint32_t ack, uint8_t flags,
			const uint8_t *payload, size_t payload_len)
{
	struct tcp_wire_inbound *c;
	struct tcp_header hdr;
	uint64_t f;
	uint32_t next_ack;
	uint32_t local_seq;
	int need_ack;

	f = ir0_spin_lock_irqsave(&tcp_lock);
	c = tcp_inbound_find(src_ip, src_port, dest_port);
	if (!c)
	{
		ir0_spin_unlock_irqrestore(&tcp_lock, f);
		return;
	}

	if (flags & TCP_FLAG_RST)
	{
		tcp_inbound_free(c);
		ir0_spin_unlock_irqrestore(&tcp_lock, f);
		return;
	}

	if (!c->established && (flags & TCP_FLAG_ACK))
		c->established = 1;

	if (payload_len > 0)
	{
		tcp_rx_append(c, payload, payload_len);
		c->peer_ack = seq + (uint32_t)payload_len;
	}

	if (flags & TCP_FLAG_FIN)
	{
		/* FIN consumes one sequence number. */
		if (payload_len == 0)
			c->peer_ack = seq + 1;
		else
			c->peer_ack = seq + (uint32_t)payload_len + 1;
		c->peer_fin = 1;
	}
	else if (payload_len == 0 && (flags & TCP_FLAG_ACK) && c->established)
	{
		(void)ack;
	}

	next_ack = c->peer_ack;
	local_seq = c->local_seq;
	need_ack = (payload_len > 0) || (flags & TCP_FLAG_FIN);
	ir0_spin_unlock_irqrestore(&tcp_lock, f);

	if (need_ack)
	{
		tcp_build_header(&hdr, dest_port, src_port, local_seq, next_ack,
				 TCP_FLAG_ACK);
		(void)tcp_send_raw(dev, src_ip, &hdr, NULL, 0);
	}
}
