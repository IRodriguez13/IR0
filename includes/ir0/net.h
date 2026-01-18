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

/* Byte order conversion macros (Big Endian <-> Little Endian) */
/* IR0 is x86-64 (Little Endian) */

#define htons(n) (((((uint16_t)(n) & 0xFF)) << 8) | (((uint16_t)(n) & 0xFF00) >> 8))
#define ntohs(n) htons(n)

#define htonl(n) (((((uint32_t)(n) & 0x000000FF)) << 24) | \
                  ((((uint32_t)(n) & 0x0000FF00)) << 8) |  \
                  ((((uint32_t)(n) & 0x00FF0000)) >> 8) |  \
                  ((((uint32_t)(n) & 0xFF000000)) >> 24))
#define ntohl(n) htonl(n)

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

/* Function to create an IP address from 4 octets */
static inline ip4_addr_t make_ip4_addr(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    /* Returns IPv4 address in network byte order */
    return (a << 24) | (b << 16) | (c << 8) | d;
}

/* --- Networking Abstraction Layer --- */

/* Standard Network Device Flags */
#define IFF_UP (1 << 0)        /* Interface is up */
#define IFF_BROADCAST (1 << 1) /* Broadcast address valid */
#define IFF_LOOPBACK (1 << 2)  /* Is a loopback net */
#define IFF_RUNNING (1 << 3)   /* Interface is running */

struct net_device
{
    const char *name;
    mac_addr_t mac;
    uint32_t flags;
    size_t mtu;
    void *priv; /* Driver private data */

    /* Driver operations */
    int (*send)(struct net_device *dev, void *data, size_t len);

    struct net_device *next;
};

/* --- Protocol Registration System --- */

/**
 * Protocol handler function type
 * @dev: Network device that received the packet
 * @data: Pointer to protocol payload (after Ethernet/IP headers)
 * @len: Length of protocol payload
 * @priv: Private data passed during registration
 */
typedef void (*net_protocol_handler_t)(struct net_device *dev, const void *data, size_t len, void *priv);

/**
 * Network protocol registration structure
 */
struct net_protocol
{
    const char *name;              /* Protocol name (e.g., "ARP", "IP", "ICMP") */
    uint16_t ethertype;            /* Ethernet type for Layer 2 protocols (ARP, IP) */
    uint8_t ipproto;               /* IP protocol number for Layer 3+ protocols (ICMP, TCP, UDP) */
    net_protocol_handler_t handler; /* Handler function */
    void *priv;                    /* Private data passed to handler */
    
    struct net_protocol *next;
};

/* Core Networking API */
int net_register_device(struct net_device *dev);
void net_unregister_device(struct net_device *dev);
struct net_device *net_get_devices(void);

/* Protocol Registration API */
int net_register_protocol(struct net_protocol *proto);
void net_unregister_protocol(struct net_protocol *proto);
struct net_protocol *net_find_protocol_by_ethertype(uint16_t ethertype);
struct net_protocol *net_find_protocol_by_ipproto(uint8_t ipproto);

/* Protocol to Driver */
int net_send(struct net_device *dev, uint16_t ethertype, const uint8_t *dest_mac, const void *payload, size_t len);

/* Driver to Protocol */
void net_receive(struct net_device *dev, const void *data, size_t len);

/* Network Stack Initialization */
int init_net_stack(void);

/* Network Polling (for receiving packets when not actively waiting) */
void net_poll(void);
