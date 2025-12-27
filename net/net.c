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
#include <ir0/logging.h>
#include <drivers/serial/serial.h>
#include <drivers/net/rtl8139.h>
#include <drivers/net/e1000.h>
#include "arp.h"
#include "ip.h"
#include "icmp.h"
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
    LOG_INFO_FMT("NET", "net_send: dev=%p, ethertype=0x%04x, payload=%p, len=%d", 
                 dev, ethertype, payload, (int)len);
    
    if (!dev || !dev->send || !payload || len > dev->mtu)
    {
        LOG_ERROR_FMT("NET", "net_send: Invalid parameters (dev=%p, send=%p, payload=%p, len=%d, mtu=%d)",
                     dev, dev ? dev->send : NULL, payload, (int)len, dev ? (int)dev->mtu : 0);
        return -1;
    }

    size_t frame_len = sizeof(struct eth_header) + len;
    LOG_INFO_FMT("NET", "net_send: Allocating frame buffer, frame_len=%d", (int)frame_len);
    uint8_t *frame = kmalloc(frame_len);

    if (!frame)
    {
        LOG_ERROR("NET", "net_send: Failed to allocate frame buffer");
        return -1;
    }
    LOG_INFO_FMT("NET", "net_send: Frame buffer allocated at %p", frame);

    struct eth_header *eth = (struct eth_header *)frame;
    memcpy(eth->dest, dest_mac, 6);
    memcpy(eth->src, dev->mac, 6);
    eth->type = htons(ethertype);
    LOG_INFO_FMT("NET", "net_send: Ethernet header filled, copying %d bytes of payload", (int)len);

    memcpy(frame + sizeof(struct eth_header), payload, len);
    LOG_INFO("NET", "net_send: Payload copied, calling dev->send");
    
    /* CRITICAL: Log exact values before calling dev->send to catch corruption */
    LOG_INFO_FMT("NET", "net_send: About to call dev->send: dev=%p, frame=%p, frame_len=%d (0x%x)", 
                 dev, frame, (int)frame_len, (unsigned int)frame_len);
    LOG_INFO_FMT("NET", "net_send: dev->send function pointer=%p", dev->send);
    
    /* Verify frame_len is reasonable before calling */
    if (frame_len > 2000)
    {
        LOG_ERROR_FMT("NET", "net_send: CORRUPTION DETECTED! frame_len=%d is too large!", (int)frame_len);
        kfree(frame);
        return -1;
    }

    int ret = dev->send(dev, frame, frame_len);
    LOG_INFO_FMT("NET", "net_send: dev->send returned %d, freeing frame buffer", ret);

    kfree(frame);
    LOG_INFO("NET", "net_send: Frame buffer freed, returning");
    return ret;
}

void net_receive(struct net_device *dev, const void *data, size_t len)
{
    if (!dev || !data || len < sizeof(struct eth_header))
    {
        LOG_WARNING_FMT("NET", "Invalid packet: dev=%p, data=%p, len=%d", dev, data, (int)len);
        return;
    }

    struct eth_header *eth = (struct eth_header *)data;
    uint16_t type = ntohs(eth->type);

    LOG_INFO_FMT("NET", "Received packet on %s: len=%d, type=0x%04x, src_mac=%02x:%02x:%02x:%02x:%02x:%02x, dst_mac=%02x:%02x:%02x:%02x:%02x:%02x",
                 dev->name, (int)len, type,
                 eth->src[0], eth->src[1], eth->src[2], eth->src[3], eth->src[4], eth->src[5],
                 eth->dest[0], eth->dest[1], eth->dest[2], eth->dest[3], eth->dest[4], eth->dest[5]);

    /* Look up protocol handler */
    struct net_protocol *proto = net_find_protocol_by_ethertype(type);
    if (proto && proto->handler)
    {
        LOG_INFO_FMT("NET", "Found protocol handler: %s", proto->name);
        /* Extract payload (after Ethernet header) */
        const void *payload = (const uint8_t *)data + sizeof(struct eth_header);
        size_t payload_len = len - sizeof(struct eth_header);
        
        LOG_INFO_FMT("NET", "Calling protocol handler %s with payload_len=%d", proto->name, (int)payload_len);
        /* Call protocol handler */
        proto->handler(dev, payload, payload_len, proto->priv);
        LOG_INFO_FMT("NET", "Protocol handler %s returned", proto->name);
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

/**
 * init_net_stack - Initialize the complete network stack
 * 
 * This function initializes all network drivers and protocols:
 * - Network drivers (RTL8139, e1000)
 * - Network protocols (ARP, and future protocols like IP, ICMP, etc.)
 * 
 * @return: 0 on success, -1 on error
 */
int init_net_stack(void)
{
    LOG_INFO("NET", "Initializing network stack");
    
    /* Initialize network drivers */
    rtl8139_init();
    e1000_init();
    
    /* Initialize network protocols (order matters: ARP before IP, IP before ICMP) */
    if (arp_init() != 0)
    {
        LOG_ERROR("NET", "Failed to initialize ARP protocol");
        return -1;
    }
    
    if (ip_init() != 0)
    {
        LOG_ERROR("NET", "Failed to initialize IP protocol");
        return -1;
    }
    
    if (icmp_init() != 0)
    {
        LOG_ERROR("NET", "Failed to initialize ICMP protocol");
        return -1;
    }
    
    LOG_INFO("NET", "Network stack initialized successfully");
    return 0;
}
