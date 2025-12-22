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

    /* Dispatch to protocols */
    switch (type)
    {
    case ETHERTYPE_ARP:
        serial_print("NET: ARP packet dispatch (not implemented)\n");
        break;
    case ETHERTYPE_IP:
        serial_print("NET: IP packet dispatch (not implemented)\n");
        break;
    default:
        serial_print("NET: Unknown EtherType\n");
        break;
    }
}

struct net_device *net_get_devices(void)
{
    return devices;
}
