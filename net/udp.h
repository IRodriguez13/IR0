/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: udp.h
 * Description: UDP (User Datagram Protocol) header and API definitions
 */

#pragma once

#include <ir0/net.h>
#include <stdint.h>
#include <stddef.h>

/* UDP Header Structure (RFC 768) */
struct udp_header
{
    uint16_t src_port;      /* Source port */
    uint16_t dest_port;     /* Destination port */
    uint16_t length;        /* UDP header + data length */
    uint16_t checksum;      /* UDP checksum (0 = disabled) */
} __attribute__((packed));

/* UDP Protocol API */
int udp_init(void);
int udp_send(struct net_device *dev, ip4_addr_t dest_ip, uint16_t src_port,
             uint16_t dest_port, const void *data, size_t len);
void udp_receive_handler(struct net_device *dev, const void *data,
                         size_t len, void *priv);
void udp_register_handler(uint16_t port, void (*handler)(struct net_device *dev, ip4_addr_t src_ip,
                                                          uint16_t src_port, const void *data, size_t len));

/* UDP Checksum Calculation */
uint16_t udp_checksum(const void *data, size_t len, ip4_addr_t src_ip, ip4_addr_t dest_ip);

