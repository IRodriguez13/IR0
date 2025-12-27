/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: icmp.h
 * Description: ICMP (Internet Control Message Protocol) header and API definitions
 */

#pragma once

#include <ir0/net.h>
#include <stdint.h>
#include <stddef.h>

/* ICMP Message Types (RFC 792) */
#define ICMP_TYPE_ECHO_REPLY     0
#define ICMP_TYPE_DEST_UNREACH   3
#define ICMP_TYPE_SOURCE_QUENCH  4
#define ICMP_TYPE_REDIRECT       5
#define ICMP_TYPE_ECHO_REQUEST   8
#define ICMP_TYPE_TIME_EXCEEDED  11
#define ICMP_TYPE_PARAM_PROBLEM  12
#define ICMP_TYPE_TIMESTAMP      13
#define ICMP_TYPE_TIMESTAMP_REPLY 14

/* ICMP Code Values */
#define ICMP_CODE_NET_UNREACH    0  /* Network Unreachable */
#define ICMP_CODE_HOST_UNREACH   1  /* Host Unreachable */
#define ICMP_CODE_PROTO_UNREACH  2  /* Protocol Unreachable */
#define ICMP_CODE_PORT_UNREACH   3  /* Port Unreachable */

/* ICMP Header Structure (RFC 792) */
struct icmp_header
{
    uint8_t type;           /* ICMP message type */
    uint8_t code;           /* ICMP message code */
    uint16_t checksum;      /* ICMP checksum */
    /* Type-specific data follows */
    union {
        struct {
            uint16_t id;       /* Echo Request/Reply: Identifier */
            uint16_t seq;       /* Echo Request/Reply: Sequence Number */
        } echo;
        uint32_t gateway;       /* Redirect: Gateway address */
        struct {
            uint16_t unused;
            uint16_t mtu;       /* Fragmentation Needed: Next-Hop MTU */
        } frag;
    } un;
} __attribute__((packed));

/* ICMP Echo Request/Reply Payload */
struct icmp_echo_payload
{
    uint8_t data[0];  /* Variable length data */
};

/* ICMP Protocol API */
int icmp_init(void);
int icmp_send_echo_request(struct net_device *dev, ip4_addr_t dest_ip, 
                           uint16_t id, uint16_t seq, const void *data, size_t len);
void icmp_receive_handler(struct net_device *dev, const void *data, 
                           size_t len, void *priv);

/* ICMP Checksum Calculation */
uint16_t icmp_checksum(const void *data, size_t len);

