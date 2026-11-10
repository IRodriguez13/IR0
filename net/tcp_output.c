/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: tcp_output.c
 * Description: Wire TCP segment output — header build, checksum,
 *              source address and ephemeral ports.
 */

#include "ip.h"
#include "arp.h"
#include <ir0/kmem.h>
#include "tcp_internal.h"

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

	{
		uint16_t pw[sizeof(pseudo) / 2];

		memcpy(pw, &pseudo, sizeof(pseudo));
		for (i = 0; i < sizeof(pseudo) / 2; i++)
			sum += ntohs(pw[i]);
	}

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

int tcp_send_raw(struct net_device *dev, ip4_addr_t dest_ip,
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
	if (payload_len > UINT16_MAX - TCP_HDR_LEN)
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

uint16_t tcp_pick_ephemeral(void)
{
	static uint16_t next = 40000;

	next++;
	if (next < 32768 || next > 60999)
		next = 32768;
	return next;
}

int tcp_build_header(struct tcp_header *hdr, uint16_t src_port,
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
