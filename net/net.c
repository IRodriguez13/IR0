/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: net.c
 * Description: Networking abstraction layer core implementation.
 */

#include <ir0/net.h>
#include <ir0/memory/kmem.h>
#include <drivers/serial/serial.h>
#include <string.h>

static struct net_device *devices = NULL;
static struct net_protocol *protocols = NULL;

int net_register_device(struct net_device *dev)
{
    if (!dev)
        return -1;

    /* Check if already registered */
    struct net_device *curr = devices;
    while (curr)
    {
        if (curr == dev)
        {
            serial_print("NET: Device already registered: ");
            serial_print(dev->name);
            serial_print("\n");
            return 0;
        }
        curr = curr->next;
    }

    dev->next = devices;
    devices = dev;

    serial_print("NET: Registered device: ");
    serial_print(dev->name);
    serial_print("\n");

    return 0;
}

void net_unregister_device(struct net_device *dev)
{
    if (!dev || !devices)
        return;

    if (devices == dev)
    {
        devices = dev->next;
        return;
    }

    struct net_device *curr = devices;
    while (curr->next)
    {
        if (curr->next == dev)
        {
            curr->next = dev->next;
            return;
        }
        
        curr = curr->next;
    }
}

int net_send(struct net_device *dev, uint16_t ethertype, const uint8_t *dest_mac, const void *payload, size_t len)
{
    if (!dev || !dev->send || !payload || len > dev->mtu)
        return -1;

    size_t frame_len = sizeof(struct eth_header) + len;
    uint8_t *frame = kmalloc(frame_len);

    if (!frame)
        return -1;

    struct eth_header *eth = (struct eth_header *)frame;
    memcpy(eth->dest, dest_mac, 6);
    memcpy(eth->src, dev->mac, 6);
    eth->type = htons(ethertype);

    memcpy(frame + sizeof(struct eth_header), payload, len);

    int ret = dev->send(dev, frame, frame_len);

    kfree(frame);
    return ret;
}

void net_receive(struct net_device *dev, const void *data, size_t len)
{
    if (!dev || !data || len < sizeof(struct eth_header))
        return;

    struct eth_header *eth = (struct eth_header *)data;
    uint16_t type = ntohs(eth->type);

    serial_print("NET: Received packet on ");
    serial_print(dev->name);
    serial_print(" Type: 0x");
    serial_print_hex32(type);
    serial_print("\n");

    /* Look up protocol handler */
    struct net_protocol *proto = net_find_protocol_by_ethertype(type);
    if (proto && proto->handler)
    {
        /* Extract payload (after Ethernet header) */
        const void *payload = (const uint8_t *)data + sizeof(struct eth_header);
        size_t payload_len = len - sizeof(struct eth_header);
        
        /* Call protocol handler */
        proto->handler(dev, payload, payload_len, proto->priv);
    }
    else
    {
        serial_print("NET: No handler registered for EtherType 0x");
        serial_print_hex32(type);
        serial_print("\n");
    }
}

struct net_device *net_get_devices(void)
{
    return devices;
}

/* --- Protocol Registration System --- */

/**
 * Register a network protocol handler
 * @proto: Protocol registration structure
 * @return: 0 on success, -1 on error
 */
int net_register_protocol(struct net_protocol *proto)
{
    if (!proto || !proto->name || !proto->handler)
        return -1;

    /* Check if already registered */
    struct net_protocol *curr = protocols;
    while (curr)
    {
        if (curr == proto)
        {
            serial_print("NET: Protocol already registered: ");
            serial_print(proto->name);
            serial_print("\n");
            return 0;
        }
        curr = curr->next;
    }

    /* Add to protocol list */
    proto->next = protocols;
    protocols = proto;

    serial_print("NET: Registered protocol: ");
    serial_print(proto->name);
    if (proto->ethertype != 0)
    {
        serial_print(" (EtherType: 0x");
        serial_print_hex32(proto->ethertype);
        serial_print(")");
    }
    if (proto->ipproto != 0)
    {
        serial_print(" (IP Proto: ");
        serial_print_hex32(proto->ipproto);
        serial_print(")");
    }
    serial_print("\n");

    return 0;
}

/**
 * Unregister a network protocol handler
 * @proto: Protocol registration structure
 */
void net_unregister_protocol(struct net_protocol *proto)
{
    if (!proto || !protocols)
        return;

    if (protocols == proto)
    {
        protocols = proto->next;
        serial_print("NET: Unregistered protocol: ");
        serial_print(proto->name);
        serial_print("\n");
        return;
    }

    struct net_protocol *curr = protocols;
    while (curr->next)
    {
        if (curr->next == proto)
        {
            curr->next = proto->next;
            serial_print("NET: Unregistered protocol: ");
            serial_print(proto->name);
            serial_print("\n");
            return;
        }
        curr = curr->next;
    }
}

/**
 * Find protocol handler by Ethernet type (Layer 2)
 * @ethertype: Ethernet type (e.g., ETHERTYPE_ARP, ETHERTYPE_IP)
 * @return: Protocol structure or NULL if not found
 */
struct net_protocol *net_find_protocol_by_ethertype(uint16_t ethertype)
{
    struct net_protocol *curr = protocols;
    while (curr)
    {
        if (curr->ethertype == ethertype)
            return curr;
        curr = curr->next;
    }
    return NULL;
}

/**
 * Find protocol handler by IP protocol number (Layer 3+)
 * @ipproto: IP protocol number (e.g., IPPROTO_ICMP, IPPROTO_TCP, IPPROTO_UDP)
 * @return: Protocol structure or NULL if not found
 */
struct net_protocol *net_find_protocol_by_ipproto(uint8_t ipproto)
{
    struct net_protocol *curr = protocols;
    while (curr)
    {
        if (curr->ipproto == ipproto)
            return curr;
        curr = curr->next;
    }
    return NULL;
}
