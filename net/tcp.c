/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: tcp.c
 * Description: Minimal wire TCP for connect + send (F8-3). No retransmit.
 */

#include "tcp.h"
#include "ip.h"
#include "arp.h"
#include <ir0/kmem.h>
#include <ir0/logging.h>
#include <ir0/clock.h>
#include <ir0/arch_port.h>
#include <ir0/net.h>
#include <ir0/errno.h>
#include <string.h>

#define TCP_WIRE_TIMEOUT_MS 5000U
#define TCP_WIRE_POLL_MS    5U

static struct net_protocol tcp_proto;

struct tcp_pending_conn
{
	int active;
	ip4_addr_t peer_ip;
	uint16_t peer_port;
	uint16_t local_port;
	uint32_t local_seq;
	uint32_t peer_ack;
	int synack_seen;
};

static struct tcp_pending_conn g_pending;

static inline uint64_t tcp_irq_save(void)
{
	return (uint64_t)arch_irq_save();
}

static inline void tcp_irq_restore(uint64_t flags)
{
	arch_irq_restore((unsigned long)flags);
}

static uint16_t tcp_checksum(const void *tcp_pkt, size_t len,
			     ip4_addr_t src_ip, ip4_addr_t dest_ip)
{
	struct
	{
		ip4_addr_t src;
		ip4_addr_t dst;
		uint8_t zero;
		uint8_t protocol;
		uint16_t length;
	} __attribute__((packed)) pseudo;
	uint32_t sum = 0;
	const uint16_t *words;
	size_t i;

	pseudo.src = src_ip;
	pseudo.dst = dest_ip;
	pseudo.zero = 0;
	pseudo.protocol = IPPROTO_TCP;
	pseudo.length = htons((uint16_t)len);

	words = (const uint16_t *)&pseudo;
	for (i = 0; i < sizeof(pseudo) / 2; i++)
		sum += ntohs(words[i]);

	words = (const uint16_t *)tcp_pkt;
	for (i = 0; i < len / 2; i++)
		sum += ntohs(words[i]);
	if (len & 1)
		sum += ((const uint8_t *)tcp_pkt)[len - 1] << 8;

	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);

	return htons((uint16_t)(~sum));
}

static ip4_addr_t tcp_src_ip(struct net_device *dev)
{
	ip4_addr_t src_ip = ip_local_addr;
	ip4_addr_t iface_ip;

	if (dev && arp_get_interface_ip(dev, &iface_ip) == 0)
		src_ip = iface_ip;
	return src_ip;
}

static int tcp_send_raw(struct net_device *dev, ip4_addr_t dest_ip,
			const struct tcp_header *hdr, const void *payload,
			size_t payload_len)
{
	size_t total = TCP_HDR_LEN + payload_len;
	uint8_t *pkt;
	ip4_addr_t src_ip;
	struct tcp_header *out;
	int ret;

	if (!dev)
		return -1;

	pkt = kmalloc(total);
	if (!pkt)
		return -1;

	memcpy(pkt, hdr, TCP_HDR_LEN);
	if (payload_len > 0)
		memcpy(pkt + TCP_HDR_LEN, payload, payload_len);

	out = (struct tcp_header *)pkt;
	out->checksum = 0;
	src_ip = tcp_src_ip(dev);
	out->checksum = tcp_checksum(pkt, total, src_ip, dest_ip);

	ret = ip_send(dev, dest_ip, IPPROTO_TCP, pkt, total);
	kfree(pkt);
	return ret;
}

static uint16_t tcp_pick_ephemeral(void)
{
	static uint16_t next = 40000;

	next++;
	if (next < 32768 || next > 60999)
		next = 32768;
	return next;
}

static void tcp_pending_clear(void)
{
	uint64_t f = tcp_irq_save();

	memset(&g_pending, 0, sizeof(g_pending));
	tcp_irq_restore(f);
}

static void tcp_pending_set(ip4_addr_t peer_ip, uint16_t peer_port,
			    uint16_t local_port, uint32_t local_seq)
{
	uint64_t f = tcp_irq_save();

	memset(&g_pending, 0, sizeof(g_pending));
	g_pending.active = 1;
	g_pending.peer_ip = peer_ip;
	g_pending.peer_port = peer_port;
	g_pending.local_port = local_port;
	g_pending.local_seq = local_seq;
	tcp_irq_restore(f);
}

static int tcp_build_header(struct tcp_header *hdr, uint16_t src_port,
			    uint16_t dest_port, uint32_t seq, uint32_t ack,
			    uint8_t flags)
{
	memset(hdr, 0, sizeof(*hdr));
	hdr->src_port = htons(src_port);
	hdr->dest_port = htons(dest_port);
	hdr->seq_num = htonl(seq);
	hdr->ack_num = htonl(ack);
	hdr->data_offset = (5U << 4);
	hdr->flags = flags;
	hdr->window = htons(8192);
	return 0;
}

int tcp_wire_connect(ip4_addr_t peer_ip, uint16_t peer_port,
		     uint16_t *local_port_out, uint32_t *seq_out, uint32_t *ack_out)
{
	struct net_device *dev;
	struct tcp_header hdr;
	uint16_t lport;
	uint32_t isn;
	uint64_t deadline;
	int ret;

	if (!local_port_out || !seq_out || !ack_out || peer_port == 0)
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

	deadline = clock_get_uptime_milliseconds() + TCP_WIRE_TIMEOUT_MS;
	for (;;)
	{
		int seen;
		uint64_t f;

		net_stack_poll();
		f = tcp_irq_save();
		seen = g_pending.synack_seen;
		tcp_irq_restore(f);
		if (seen)
			break;
		if (clock_get_uptime_milliseconds() >= deadline)
		{
			tcp_pending_clear();
			return -ETIMEDOUT;
		}
		{
			uint64_t target = clock_get_uptime_milliseconds() + TCP_WIRE_POLL_MS;

			while (clock_get_uptime_milliseconds() < target)
				;
		}
	}

	tcp_build_header(&hdr, lport, peer_port, isn + 1, g_pending.peer_ack,
			 TCP_FLAG_ACK);
	ret = tcp_send_raw(dev, peer_ip, &hdr, NULL, 0);
	if (ret != 0)
	{
		tcp_pending_clear();
		return -EIO;
	}

	*local_port_out = lport;
	*seq_out = isn + 1;
	*ack_out = g_pending.peer_ack;
	tcp_pending_clear();
	return 0;
}

int tcp_wire_send(ip4_addr_t peer_ip, uint16_t peer_port, uint16_t local_port,
		  uint32_t *seq_io, uint32_t ack_peer, const void *data, size_t len)
{
	struct net_device *dev;
	struct tcp_header hdr;
	int ret;

	if (!seq_io || !data || len == 0)
		return -EINVAL;

	dev = net_get_devices();
	if (!dev)
		return -ENETUNREACH;

	tcp_build_header(&hdr, local_port, peer_port, *seq_io, ack_peer,
			 TCP_FLAG_PSH | TCP_FLAG_ACK);
	ret = tcp_send_raw(dev, peer_ip, &hdr, data, len);
	if (ret != 0)
		return -EIO;

	*seq_io += (uint32_t)len;
	return (int)len;
}

void tcp_wire_close(ip4_addr_t peer_ip, uint16_t peer_port, uint16_t local_port,
		    uint32_t seq, uint32_t ack_peer)
{
	struct net_device *dev;
	struct tcp_header hdr;

	dev = net_get_devices();
	if (!dev)
		return;

	tcp_build_header(&hdr, local_port, peer_port, seq, ack_peer,
			 TCP_FLAG_FIN | TCP_FLAG_ACK);
	(void)tcp_send_raw(dev, peer_ip, &hdr, NULL, 0);
}

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
	uint64_t f;

	(void)dev;

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

	f = tcp_irq_save();
	if (g_pending.active && !g_pending.synack_seen &&
	    g_pending.local_port == dest_port &&
	    g_pending.peer_port == src_port &&
	    g_pending.peer_ip == src_ip &&
	    (flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
	    ack == g_pending.local_seq + 1)
	{
		g_pending.synack_seen = 1;
		g_pending.peer_ack = seq + 1;
	}
	tcp_irq_restore(f);
}

int tcp_init(void)
{
	LOG_INFO("TCP", "Initializing minimal wire TCP");

	memset(&tcp_proto, 0, sizeof(tcp_proto));
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
