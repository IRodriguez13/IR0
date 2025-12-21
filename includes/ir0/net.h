/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: net.h
 * Description: Common networking types and macros for IR0.
 */

#pragma once

#include <stdint.h>

/* Byte order conversion macros (Big Endian <-> Little Endian) */
/* IR0 is x86-64 (Little Endian) */

#define htons(n) (((((uint16_t)(n) & 0xFF)) << 8) | (((uint16_t)(n) & 0xFF00) >> 8))
#define ntohs(n) htons(n)

#define htonl(n) (((((uint32_t)(n) & 0x000000FF)) << 24) | \
                  ((((uint32_t)(n) & 0x0000FF00)) << 8)  | \
                  ((((uint32_t)(n) & 0x00FF0000)) >> 8)  | \
                  ((((uint32_t)(n) & 0xFF000000)) >> 24))
#define ntohl(n) htonl(n)

/* Common Ethernet Types */
#define ETHERTYPE_IP   0x0800
#define ETHERTYPE_ARP  0x0806
#define ETHERTYPE_IPV6 0x86DD

/* Ethernet header (14 bytes) */
struct eth_header {
    uint8_t  dest[6];
    uint8_t  src[6];
    uint16_t type;
} __attribute__((packed));

/* IPv4 Address type */
typedef uint32_t ip4_addr_t;

/* MAC Address type */
typedef uint8_t mac_addr_t[6];

/* Function to create an IP address from 4 octets */
static inline ip4_addr_t make_ip4_addr(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return (a << 24) | (b << 16) | (c << 8) | d;
}
