/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: net.h
 * Description: Common networking types and macros for IR0 networking implementation.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Network order is big-endian; host order belongs to the selected ISA. */
static inline uint16_t ir0_host_to_be16(uint16_t value)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return __builtin_bswap16(value);
#else
	return value;
#endif
}

static inline uint32_t ir0_host_to_be32(uint32_t value)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return __builtin_bswap32(value);
#else
	return value;
#endif
}

#define htons(n) ir0_host_to_be16((uint16_t)(n))
#define ntohs(n) ir0_host_to_be16((uint16_t)(n))
#define htonl(n) ir0_host_to_be32((uint32_t)(n))
#define ntohl(n) ir0_host_to_be32((uint32_t)(n))

/* Common Ethernet Types */
#define ETHERTYPE_IP 0x0800
#define ETHERTYPE_ARP 0x0806
#define ETHERTYPE_IPV6 0x86DD

/* Common IP Protocol Numbers */
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

/* Ethernet header (14 bytes) */
struct eth_header
{
    uint8_t dest[6];
    uint8_t src[6];
    uint16_t type;
} __attribute__((packed));

/* IPv4 Address type */
typedef uint32_t ip4_addr_t;

/* MAC Address type */
typedef uint8_t mac_addr_t[6];

struct net_device;

/* Network Stack Initialization */
int init_net_stack(void);
/* Probe the configured NIC drivers (drivers/net/net_drivers.c). */
int net_stack_probe_drivers(void);
int net_stack_post_irq_init(void);

/*
 * Run every device's ndo_poll_controller: receive without waiting for an
 * interrupt (idle loop, protocol waits).
 */
void net_stack_poll(void);

/*
 * x86 PIC bridge until its ISR calls irq_dispatch(): the first NIC IRQ line
 * to unmask, and the per-interrupt hook that dispatches NIC lines and polls
 * the devices that have none.
 */
int net_stack_get_irq_line(void);
int net_stack_handle_irq(uint8_t irq);

/* Ping result structure for syscalls */
struct ping_result {
    int success;          /* 1 if ping succeeded, 0 if failed */
    uint16_t seq;         /* ICMP sequence number */
    uint64_t rtt;         /* Round-trip time in milliseconds */
    uint8_t ttl;          /* TTL from reply */
    size_t payload_bytes; /* Payload size in reply */
    ip4_addr_t reply_ip;  /* IP address that replied */
};

/* IPv4 interface configuration, network byte order. Owned by net/ip.c. */
struct ip_config
{
	ip4_addr_t addr;
	ip4_addr_t netmask;
	ip4_addr_t gateway;
};

void ip_get_config(struct ip_config *cfg);
void ip_set_config(const struct ip_config *cfg);

/*
 * The storage behind ip_get_config()/ip_set_config(). Only fs/devfs_net.c
 * and fs/procfs_net.c still touch it directly; they move to the accessors
 * with FS-3, and then these become static to net/ip.c.
 */
extern ip4_addr_t ip_local_addr;
extern ip4_addr_t ip_netmask;
extern ip4_addr_t ip_gateway;

/* Soft route table walk (BusyBox /proc/net/route). */
int ip_route_walk(int (*cb)(ip4_addr_t dest, ip4_addr_t mask, ip4_addr_t gw,
			     void *ctx),
		  void *ctx);
int ip_routes_seed_from_globals(void);

/* ICMP helpers used by /dev/net control plane. */
bool icmp_get_next_echo_result(uint16_t id, uint16_t *seq_out, uint64_t *rtt_out,
                               uint8_t *ttl_out, size_t *payload_bytes_out,
                               ip4_addr_t *reply_ip_out);
bool icmp_has_ready_echo_result(uint16_t id);
uint16_t icmp_allocate_echo_seq(void);
int icmp_send_echo_request(struct net_device *dev, ip4_addr_t dest_ip,
                           uint16_t id, uint16_t seq, const void *data, size_t len);

/* DNS resolver used by /dev/net ping hostname path. */
ip4_addr_t dns_resolve(const char *domain_name, ip4_addr_t dns_server_ip);
ip4_addr_t dns_get_default_server(void);

/* ARP IP synchronization helpers used by NET_SET_CONFIG. */
void arp_set_my_ip(ip4_addr_t ip);
int arp_set_interface_ip(struct net_device *dev, ip4_addr_t ip);
