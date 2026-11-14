/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: tcp_input.c
 * Description: Wire TCP receive demux — SYN-ACK for a pending
 *              connect, ACK/SACK/data for the outbound association,
 *              passive-open segments to tcp_listen.c.
 */

#include "tcp.h"
#include "ip.h"
#include <ir0/klog.h>
#include <ir0/poll.h>
#include <string.h>
#include "tcp_internal.h"

void tcp_receive_handler(struct net_device *dev, const void *data, size_t len,
			 void *priv)
{
	const struct ip_rx_context *rx_ctx = (const struct ip_rx_context *)priv;
	const struct tcp_header *tcp;
	ip4_addr_t src_ip;
	uint16_t src_port;
	uint16_t dest_port;
	uint8_t flags;
	size_t hdr_len;
	uint32_t seq;
	uint32_t ack;
	uint16_t window;
	uint64_t f;
	const uint8_t *payload;
	size_t payload_len;

	if (len < TCP_HDR_LEN)
		return;

	tcp = (const struct tcp_header *)data;
	src_port = ntohs(tcp->src_port);
	dest_port = ntohs(tcp->dest_port);
	flags = tcp->flags;
	hdr_len = (size_t)((tcp->data_offset >> 4) * 4);
	if (hdr_len < TCP_HDR_LEN || hdr_len > len)
		return;

	src_ip = rx_ctx ? rx_ctx->src_addr : 0;
	seq = ntohl(tcp->seq_num);
	ack = ntohl(tcp->ack_num);
	window = ntohs(tcp->window);
	payload = (const uint8_t *)data + hdr_len;
	payload_len = len - hdr_len;

	f = ir0_spin_lock_irqsave(&tcp_lock);
	if (tcp_wire_pending.active && !tcp_wire_pending.synack_seen &&
	    tcp_wire_pending.local_port == dest_port &&
	    tcp_wire_pending.peer_port == src_port &&
	    tcp_wire_pending.peer_ip == src_ip &&
	    (flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
	    ack == tcp_wire_pending.local_seq + 1)
	{
		tcp_wire_pending.synack_seen = 1;
		tcp_wire_pending.peer_ack = seq + 1;
		tcp_wire_pending.peer_window = window ? window : 8192;
		ir0_spin_unlock_irqrestore(&tcp_lock, f);
		(void)poll_wake_check_nosched();
		return;
	}
	if (tcp_wire_out.active && tcp_wire_out.local_port == dest_port &&
	    tcp_wire_out.peer_port == src_port && tcp_wire_out.peer_ip == src_ip &&
	    (flags & TCP_FLAG_ACK))
	{
		int wake = 0;

		if (hdr_len > TCP_HDR_LEN)
			tcp_parse_sack_options((const uint8_t *)data + TCP_HDR_LEN,
					       hdr_len - TCP_HDR_LEN);
		tcp_out_note_ack(ack, window ? window : tcp_wire_out.peer_window);
		if (payload_len > 0)
		{
			size_t space;
			size_t ncopy;

			if (tcp_wire_out.rx_off > 0 && tcp_wire_out.rx_off == tcp_wire_out.rx_len)
			{
				tcp_wire_out.rx_off = 0;
				tcp_wire_out.rx_len = 0;
			}
			else if (tcp_wire_out.rx_off > 0)
			{
				memmove(tcp_wire_out.rx, tcp_wire_out.rx + tcp_wire_out.rx_off,
					tcp_wire_out.rx_len - tcp_wire_out.rx_off);
				tcp_wire_out.rx_len -= tcp_wire_out.rx_off;
				tcp_wire_out.rx_off = 0;
			}
			space = TCP_WIRE_RX_MAX - tcp_wire_out.rx_len;
			ncopy = payload_len;
			if (ncopy > space)
				ncopy = space;
			if (ncopy > 0)
			{
				memcpy(tcp_wire_out.rx + tcp_wire_out.rx_len, payload, ncopy);
				tcp_wire_out.rx_len += (unsigned)ncopy;
				tcp_wire_out.ack_to_peer = seq + (uint32_t)ncopy;
				wake = 1;
			}
		}
		if (flags & TCP_FLAG_FIN)
		{
			tcp_wire_out.peer_fin = 1;
			wake = 1;
		}
		{
			int print_cwnd = tcp_wire_out.cwnd_print_pending;
			int print_sack = tcp_wire_out.sack_print_pending;

			if (print_cwnd)
				tcp_wire_out.cwnd_print_pending = 0;
			if (print_sack)
				tcp_wire_out.sack_print_pending = 0;
			ir0_spin_unlock_irqrestore(&tcp_lock, f);
			if (print_cwnd)
				klog_print("F8_TCP_WIRE_CWND_OK\n");
			if (print_sack)
				klog_print("F8_TCP_WIRE_SACK_OK\n");
			if (wake)
				(void)poll_wake_check_nosched();
		}
		return;
	}
	ir0_spin_unlock_irqrestore(&tcp_lock, f);

	/* Inbound SYN to a registered listener (host→guest). */
	if ((flags & TCP_FLAG_SYN) && !(flags & TCP_FLAG_ACK))
	{
		tcp_handle_listen_syn(dev, src_ip, src_port, dest_port, seq);
		return;
	}

	tcp_handle_inbound(dev, src_ip, src_port, dest_port, seq, ack, flags,
			   payload, payload_len);
}
