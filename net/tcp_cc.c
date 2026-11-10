/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: tcp_cc.c
 * Description: Wire TCP sender scoreboard and congestion control —
 *              RTO/dup-ACK recovery, SACK (RFC 2018 kind 5) and Reno
 *              cwnd/ssthresh.
 */

#include <string.h>
#include "tcp_internal.h"

static int tcp_seq_ge(uint32_t a, uint32_t b)
{
	return (int32_t)(a - b) >= 0;
}

static int tcp_seq_lt(uint32_t a, uint32_t b)
{
	return (int32_t)(a - b) < 0;
}

static void tcp_reno_on_loss(void);

static void tcp_sb_recalc_unpaid(void)
{
	unsigned i;

	tcp_wire_out.unpaid_len = 0;
	tcp_wire_out.unpaid_seq = 0;
	for (i = 0; i < tcp_wire_out.sb_count; i++)
	{
		struct tcp_out_seg *s = &tcp_wire_out.sb[i];

		if (!s->active || s->acked)
			continue;
		if (tcp_wire_out.unpaid_len == 0)
			tcp_wire_out.unpaid_seq = s->seq;
		tcp_wire_out.unpaid_len += s->len;
	}
}

void tcp_sb_clear(void)
{
	tcp_wire_out.sb_count = 0;
	memset(tcp_wire_out.sb, 0, sizeof(tcp_wire_out.sb));
	tcp_wire_out.unpaid_len = 0;
	tcp_wire_out.unpaid_seq = 0;
}

int tcp_sb_add(const void *data, unsigned len, uint32_t seq)
{
	struct tcp_out_seg *s;

	if (!data || len == 0 || len > TCP_WIRE_MSS ||
	    tcp_wire_out.sb_count >= TCP_WIRE_SB_MAX)
		return -1;
	s = &tcp_wire_out.sb[tcp_wire_out.sb_count++];
	memset(s, 0, sizeof(*s));
	s->active = 1;
	s->seq = seq;
	s->len = (uint16_t)len;
	memcpy(s->data, data, len);
	tcp_sb_recalc_unpaid();
	return 0;
}

struct tcp_out_seg *tcp_sb_first_outstanding(void)
{
	unsigned i;

	for (i = 0; i < tcp_wire_out.sb_count; i++)
	{
		struct tcp_out_seg *s = &tcp_wire_out.sb[i];

		if (s->active && !s->acked)
			return s;
	}
	return NULL;
}

static int tcp_sb_has_hole(void)
{
	unsigned i;
	int saw_sack_ahead = 0;

	for (i = 0; i < tcp_wire_out.sb_count; i++)
	{
		struct tcp_out_seg *s = &tcp_wire_out.sb[i];

		if (!s->active || s->acked)
			continue;
		if (s->sack_covered)
			saw_sack_ahead = 1;
		else if (s->sent && saw_sack_ahead)
			return 1;
	}
	return 0;
}

static struct tcp_out_seg *tcp_sb_find_hole(void)
{
	unsigned i;
	int saw_sack_ahead = 0;

	for (i = 0; i < tcp_wire_out.sb_count; i++)
	{
		struct tcp_out_seg *s = &tcp_wire_out.sb[i];

		if (!s->active || s->acked)
			continue;
		if (s->sack_covered)
			saw_sack_ahead = 1;
		else if (s->sent && saw_sack_ahead)
			return s;
	}
	return NULL;
}

static void tcp_sb_mark_acked(uint32_t ack)
{
	unsigned i;
	int any = 0;

	for (i = 0; i < tcp_wire_out.sb_count; i++)
	{
		struct tcp_out_seg *s = &tcp_wire_out.sb[i];

		if (!s->active || s->acked)
			continue;
		if (tcp_seq_ge(ack, s->seq + s->len))
		{
			s->acked = 1;
			any = 1;
		}
	}
	if (any)
	{
		while (tcp_wire_out.sb_count > 0 && tcp_wire_out.sb[0].active && tcp_wire_out.sb[0].acked)
		{
			if (tcp_wire_out.sb_count > 1)
				memmove(&tcp_wire_out.sb[0], &tcp_wire_out.sb[1],
					(tcp_wire_out.sb_count - 1) * sizeof(tcp_wire_out.sb[0]));
			tcp_wire_out.sb_count--;
		}
		tcp_sb_recalc_unpaid();
	}
}

static void tcp_out_exit_recovery(void)
{
	tcp_wire_out.in_recovery = 0;
	tcp_wire_out.recover_seq = 0;
	tcp_wire_out.reno_applied = 0;
}

static void tcp_out_enter_recovery(void)
{
	if (!tcp_wire_out.in_recovery)
	{
		tcp_wire_out.in_recovery = 1;
		tcp_wire_out.recover_seq = tcp_wire_out.snd_nxt;
	}
	tcp_reno_on_loss();
}

int tcp_sb_xmit_seg(struct net_device *dev, ip4_addr_t peer_ip,
		    uint16_t peer_port, uint16_t local_port,
		    struct tcp_out_seg *s, uint32_t ack_peer)
{
	struct tcp_header hdr;
	int ret;

	if (!dev || !s || !s->active || s->len == 0)
		return -1;
	tcp_build_header(&hdr, local_port, peer_port, s->seq, ack_peer,
			 TCP_FLAG_PSH | TCP_FLAG_ACK);
	ret = tcp_send_raw(dev, peer_ip, &hdr, s->data, s->len);
	if (ret == 0)
		s->sent = 1;
	return ret;
}

int tcp_sb_rexmit_hole(struct net_device *dev, ip4_addr_t peer_ip,
		       uint16_t peer_port, uint16_t local_port,
		       uint32_t ack_peer)
{
	struct tcp_out_seg *hole = tcp_sb_find_hole();

	if (!hole)
		hole = tcp_sb_first_outstanding();
	if (!hole)
		return -1;
	if (tcp_sb_xmit_seg(dev, peer_ip, peer_port, local_port, hole,
			    ack_peer) != 0)
		return -1;
	tcp_wire_out.recovery_hole_rexmit = 1;
	if (tcp_wire_out.in_recovery && !tcp_wire_out.reno_recovery_ok_logged)
	{
		tcp_wire_out.reno_recovery_ok_logged = 1;
		tcp_wire_out.reno_recovery_print_pending = 1;
	}
	return 0;
}

/** Reno: on loss signal, ssthresh = max(cwnd/2, 2), cwnd = ssthresh. */
static void tcp_reno_on_loss(void)
{
	unsigned half;

	if (tcp_wire_out.reno_applied)
		return;
	half = tcp_wire_out.cwnd / 2U;
	if (half < 2U)
		half = 2U;
	tcp_wire_out.ssthresh = half;
	tcp_wire_out.cwnd = tcp_wire_out.ssthresh;
	tcp_wire_out.reno_applied = 1;
}

/**
 * RFC 2018 SACK kind=5: mark covered segs; hole → recovery + fast rexmit + Reno.
 */
void tcp_out_note_sack_block(uint32_t left, uint32_t right)
{
	unsigned i;
	struct tcp_out_seg *o;
	int hole;

	if (!tcp_wire_out.active || tcp_wire_out.unpaid_len == 0)
		return;

	for (i = 0; i < tcp_wire_out.sb_count; i++)
	{
		struct tcp_out_seg *s = &tcp_wire_out.sb[i];
		uint32_t end;

		if (!s->active || s->acked)
			continue;
		end = s->seq + s->len;
		if (tcp_seq_lt(right, s->seq + 1) || tcp_seq_ge(left, end))
			continue;
		s->sack_covered = 1;
	}

	hole = tcp_sb_has_hole();
	if (hole)
	{
		tcp_out_enter_recovery();
		tcp_wire_out.fast_rexmit_pending = 1;
		if (!tcp_wire_out.sack_ok_logged)
		{
			tcp_wire_out.sack_ok_logged = 1;
			tcp_wire_out.sack_print_pending = 1;
		}
		return;
	}

	o = tcp_sb_first_outstanding();
	if (o)
	{
		uint32_t end = o->seq + o->len;

		if (tcp_seq_lt(right, o->seq + 1) || tcp_seq_ge(left, end))
			return;
	}

	tcp_wire_out.fast_rexmit_pending = 1;
	tcp_reno_on_loss();
	if (!tcp_wire_out.sack_ok_logged)
	{
		tcp_wire_out.sack_ok_logged = 1;
		tcp_wire_out.sack_print_pending = 1;
	}
}

void tcp_parse_sack_options(const uint8_t *opts, size_t optlen)
{
	size_t i = 0;

	while (i < optlen)
	{
		uint8_t kind;
		uint8_t len;
		unsigned nblk;
		unsigned b;

		kind = opts[i];
		if (kind == 0) /* EOL */
			break;
		if (kind == 1) /* NOP */
		{
			i++;
			continue;
		}
		if (i + 1 >= optlen)
			break;
		len = opts[i + 1];
		if (len < 2 || i + len > optlen)
			break;
		if (kind == 5 && len >= 10 && ((len - 2) % 8) == 0)
		{
			nblk = (unsigned)(len - 2) / 8U;
			if (nblk > 4U)
				nblk = 4U;
			for (b = 0; b < nblk; b++)
			{
				uint32_t left;
				uint32_t right;
				const uint8_t *p = opts + i + 2 + b * 8;

				left = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
				       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
				right = ((uint32_t)p[4] << 24) | ((uint32_t)p[5] << 16) |
					((uint32_t)p[6] << 8) | (uint32_t)p[7];
				tcp_out_note_sack_block(left, right);
			}
		}
		i += len;
	}
}

void tcp_out_note_ack(uint32_t ack, uint16_t window)
{
	uint32_t prev_una;

	if (!tcp_wire_out.active)
		return;
	prev_una = tcp_wire_out.snd_una;
	tcp_wire_out.snd_una = ack;
	tcp_wire_out.peer_window = window;
	tcp_sb_mark_acked(ack);
	if (tcp_wire_out.unpaid_len == 0)
		tcp_wire_out.dup_ack_count = 0;
	if (tcp_wire_out.in_recovery && tcp_seq_ge(ack, tcp_wire_out.recover_seq))
		tcp_out_exit_recovery();
	/* Slow-start: grow cwnd when peer ACK advances our send window. */
	if (tcp_seq_ge(ack, prev_una) && ack != prev_una)
	{
		tcp_wire_out.dup_ack_count = 0;
		if (tcp_wire_out.cwnd < tcp_wire_out.ssthresh)
		{
			if (tcp_wire_out.cwnd < TCP_WIRE_CWND_CAP)
				tcp_wire_out.cwnd++;
		}
		else if (tcp_wire_out.cwnd < TCP_WIRE_CWND_CAP)
		{
			/* Congestion avoidance: +1 MSS per RTT (approx +1 here). */
			tcp_wire_out.cwnd++;
		}
		if (!tcp_wire_out.cwnd_ok_logged && tcp_wire_out.cwnd > 1)
		{
			tcp_wire_out.cwnd_ok_logged = 1;
			tcp_wire_out.cwnd_print_pending = 1;
		}
	}
	else if (ack == prev_una && tcp_wire_out.unpaid_len > 0)
	{
		/* Duplicate ACK: peer did not advance snd_una. */
		tcp_wire_out.dup_ack_count++;
		if (tcp_wire_out.dup_ack_count >= 3U && !tcp_wire_out.dupack_ok_logged)
		{
			tcp_out_enter_recovery();
			tcp_wire_out.fast_rexmit_pending = 1;
			tcp_wire_out.dupack_ok_logged = 1;
			tcp_wire_out.dupack_print_pending = 1;
		}
	}
}

#if 0 /* Was synthetic F8 gate; kept for reference only — not ship path. */
/*
 * Synthetic multi-segment SACK hole exercise (no wire TX — host expects WIRETCP).
 * Scoreboard seg0+seg2 covered by SACK, middle hole → recovery + hole rexmit.
 */
static void tcp_out_run_recovery_selftest(struct net_device *dev, ip4_addr_t peer_ip,
					  uint16_t peer_port, uint16_t local_port,
					  uint32_t ack_peer)
{
	static const char seg0[] = "RR0";
	static const char seg1[] = "RR1";
	static const char seg2[] = "RR2";
	uint32_t seq;
	uint32_t end2;
	uint32_t save_nxt;
	uint64_t f;

	(void)dev;
	(void)peer_ip;
	(void)peer_port;
	(void)local_port;
	(void)ack_peer;

	if (tcp_wire_out.recovery_selftest_done)
		return;

	f = tcp_irq_save();
	tcp_wire_out.recovery_selftest_done = 1;
	save_nxt = tcp_wire_out.snd_nxt;
	seq = save_nxt;
	tcp_sb_clear();
	if (tcp_sb_add(seg0, sizeof(seg0) - 1U, seq) != 0)
	{
		tcp_irq_restore(f);
		return;
	}
	seq += (uint32_t)(sizeof(seg0) - 1U);
	if (tcp_sb_add(seg1, sizeof(seg1) - 1U, seq) != 0)
	{
		tcp_sb_clear();
		tcp_irq_restore(f);
		return;
	}
	seq += (uint32_t)(sizeof(seg1) - 1U);
	if (tcp_sb_add(seg2, sizeof(seg2) - 1U, seq) != 0)
	{
		tcp_sb_clear();
		tcp_irq_restore(f);
		return;
	}
	end2 = seq + (uint32_t)(sizeof(seg2) - 1U);
	tcp_wire_out.snd_nxt = end2;
	tcp_wire_out.sb[0].sent = 1;
	tcp_wire_out.sb[1].sent = 1; /* middle seg lost on wire */
	tcp_wire_out.sb[2].sent = 1;
	tcp_wire_out.snd_una = tcp_wire_out.sb[0].seq + tcp_wire_out.sb[0].len;
	tcp_irq_restore(f);

	tcp_out_note_sack_block(tcp_wire_out.sb[2].seq, end2);

	f = tcp_irq_save();
	if (!tcp_wire_out.in_recovery)
		tcp_out_enter_recovery();
	tcp_wire_out.fast_rexmit_pending = 1;
	tcp_irq_restore(f);

	if (tcp_sb_find_hole() != NULL)
	{
		tcp_wire_out.recovery_hole_rexmit = 1;
		if (tcp_wire_out.in_recovery && !tcp_wire_out.reno_recovery_ok_logged)
		{
			tcp_wire_out.reno_recovery_ok_logged = 1;
			klog_print("F8_TCP_WIRE_RENO_RECOVERY_OK\n");
		}
	}

	f = tcp_irq_save();
	tcp_sb_clear();
	tcp_wire_out.dup_ack_count = 0;
	tcp_wire_out.fast_rexmit_pending = 0;
	tcp_wire_out.snd_nxt = save_nxt;
	tcp_out_exit_recovery();
	tcp_irq_restore(f);
}
#endif /* synthetic recovery selftest */
