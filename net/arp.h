/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arp.h
 * Description: ARP (Address Resolution Protocol) implementation header
 */

#pragma once

#include <ir0/net.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ARP Operation Codes */
#define ARP_OP_REQUEST  1  /* ARP Request */
#define ARP_OP_REPLY    2  /* ARP Reply */

/* ARP Hardware Types */
#define ARP_HW_TYPE_ETHERNET 1

/* ARP Protocol Types */
#define ARP_PROTO_TYPE_IPV4 0x0800

/* ARP Header Structure (RFC 826) */
struct arp_header {
    uint16_t hw_type;      /* Hardware type (1 = Ethernet) */
    uint16_t proto_type;   /* Protocol type (0x0800 = IPv4) */
    uint8_t hw_len;        /* Hardware address length (6 for MAC) */
    uint8_t proto_len;     /* Protocol address length (4 for IPv4) */
    uint16_t opcode;       /* Operation (1 = Request, 2 = Reply) */
    uint8_t sender_mac[6];
    uint32_t sender_ip;
    uint8_t target_mac[6];
    uint32_t target_ip;
} __attribute__((packed));

/* ARP Cache Entry */
struct arp_cache_entry {
    ip4_addr_t ip;
    mac_addr_t mac;
    uint64_t timestamp;            /* Cache entry timestamp (milliseconds) */
    struct arp_cache_entry *next;
};

/* Public API */
int arp_init(void);
int arp_resolve(struct net_device *dev, ip4_addr_t ip, mac_addr_t mac);
struct arp_cache_entry *arp_lookup(ip4_addr_t ip);
void arp_cache_add(ip4_addr_t ip, const mac_addr_t mac);
void arp_send_request(struct net_device *dev, ip4_addr_t target_ip);
void arp_print_cache(void);
void arp_set_my_ip(ip4_addr_t ip);  /* Update ARP's default IP address (sync with IP layer) */
int arp_set_interface_ip(struct net_device *dev, ip4_addr_t ip);  /* Set IP per interface */
int arp_get_interface_ip(struct net_device *dev, ip4_addr_t *ip);  /* Get IP for interface */

