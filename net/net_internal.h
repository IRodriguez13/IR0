/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: net_internal.h
 * Description: Protocol registration and frame output shared by the net/
 *              protocol units. Not a public API: drivers and other
 *              subsystems use <ir0/net.h>.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <ir0/net.h>

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

/* Protocol Registration API */
int net_register_protocol(struct net_protocol *proto);
struct net_protocol *net_find_protocol_by_ipproto(uint8_t ipproto);

/* Protocol to Driver */
int net_send(struct net_device *dev, uint16_t ethertype, const uint8_t *dest_mac, const void *payload, size_t len);
