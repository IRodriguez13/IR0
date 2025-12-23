/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arp.c
 * Description: ARP (Address Resolution Protocol) implementation
 */

#include "arp.h"
#include <ir0/memory/kmem.h>
#include <ir0/logging.h>
#include <drivers/serial/serial.h>
#include <string.h>

/* Macros for IP address formatting */
#define IP4_FMT "%d.%d.%d.%d"
#define IP4_ARGS(ip) \
    (int)((ip >> 24) & 0xFF), \
    (int)((ip >> 16) & 0xFF), \
    (int)((ip >> 8) & 0xFF), \
    (int)(ip & 0xFF)

/* ARP Cache */
static struct arp_cache_entry *arp_cache = NULL;

/* Our IP address (should be configured per interface, simplified for now) */
static ip4_addr_t my_ip = 0;

/* ARP Protocol registration */
static struct net_protocol arp_proto;

/* Broadcast MAC address */
static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/**
 * arp_receive_handler - Handle incoming ARP packets
 * @dev: Network device that received the packet
 * @data: Pointer to ARP packet data
 * @len: Length of ARP packet
 * @priv: Private data (unused)
 */
static void arp_receive_handler(struct net_device *dev, const void *data, 
                                 size_t len, void *priv)
{
    (void)priv;
    
    if (len < sizeof(struct arp_header))
    {
        LOG_WARNING("ARP", "Packet too short");
        return;
    }
    
    const struct arp_header *arp = (const struct arp_header *)data;
    
    /* Validate ARP packet */
    if (ntohs(arp->hw_type) != ARP_HW_TYPE_ETHERNET ||
        ntohs(arp->proto_type) != ARP_PROTO_TYPE_IPV4 ||
        arp->hw_len != 6 ||
        arp->proto_len != 4)
    {
        LOG_WARNING("ARP", "Invalid ARP packet format");
        return;
    }
    
    uint16_t opcode = ntohs(arp->opcode);
    ip4_addr_t sender_ip = ntohl(arp->sender_ip);  /* Convert to host byte order */
    ip4_addr_t target_ip = ntohl(arp->target_ip);  /* Convert to host byte order */
    
    LOG_INFO_FMT("ARP", "Received ARP packet: opcode=%d", (int)opcode);
    
    if (opcode == ARP_OP_REQUEST)
    {
        LOG_INFO("ARP", "ARP Request received");
        
        /* Update ARP cache with sender information */
        arp_cache_add(sender_ip, arp->sender_mac);
        
        /* Check if request is for us */
        if (target_ip == my_ip)
        {
            LOG_INFO("ARP", "ARP Request is for us, sending reply");
            
            /* Send ARP Reply */
            struct arp_header *reply = (struct arp_header *)kmalloc(sizeof(struct arp_header));
            if (!reply)
                return;
            
            memset(reply, 0, sizeof(struct arp_header));
            reply->hw_type = htons(ARP_HW_TYPE_ETHERNET);
            reply->proto_type = htons(ARP_PROTO_TYPE_IPV4);
            reply->hw_len = 6;
            reply->proto_len = 4;
            reply->opcode = htons(ARP_OP_REPLY);
            
            /* Sender (us) */
            memcpy(reply->sender_mac, dev->mac, 6);
            reply->sender_ip = htonl(my_ip);
            
            /* Target (request sender) */
            memcpy(reply->target_mac, arp->sender_mac, 6);
            reply->target_ip = arp->sender_ip;
            
            /* Send reply to the sender */
            if (net_send(dev, ETHERTYPE_ARP, arp->sender_mac, reply, sizeof(struct arp_header)) == 0)
            {
                LOG_INFO("ARP", "ARP Reply sent");
            }
            else
            {
                LOG_ERROR("ARP", "Failed to send ARP Reply");
            }
            
            kfree(reply);
        }
    }
    else if (opcode == ARP_OP_REPLY)
    {
        LOG_INFO("ARP", "ARP Reply received");
        
        /* Update ARP cache with sender information */
        arp_cache_add(sender_ip, arp->sender_mac);
        
        LOG_INFO_FMT("ARP", "Resolved IP " IP4_FMT " -> MAC %02x:%02x:%02x:%02x:%02x:%02x",
                     IP4_ARGS(sender_ip),
                     arp->sender_mac[0], arp->sender_mac[1], arp->sender_mac[2],
                     arp->sender_mac[3], arp->sender_mac[4], arp->sender_mac[5]);
    }
}

/**
 * arp_init - Initialize ARP protocol
 * @return: 0 on success, -1 on error
 */
int arp_init(void)
{
    LOG_INFO("ARP", "Initializing ARP protocol");
    
    memset(&arp_proto, 0, sizeof(arp_proto));
    arp_proto.name = "ARP";
    arp_proto.ethertype = ETHERTYPE_ARP;
    arp_proto.handler = arp_receive_handler;
    arp_proto.priv = NULL;
    
    /* Set default IP (should be configurable per interface) */
    my_ip = make_ip4_addr(192, 168, 1, 100);
    
    int ret = net_register_protocol(&arp_proto);
    if (ret == 0)
    {
        LOG_INFO("ARP", "ARP protocol registered");
        /* Print initial cache state (should be empty) */
        arp_print_cache();
    }
    else
    {
        LOG_ERROR("ARP", "Failed to register ARP protocol");
    }
    
    return ret;
}

/**
 * arp_lookup - Look up MAC address for given IP in ARP cache
 * @ip: IP address to look up
 * @return: ARP cache entry or NULL if not found
 */
struct arp_cache_entry *arp_lookup(ip4_addr_t ip)
{
    struct arp_cache_entry *entry = arp_cache;
    
    while (entry)
    {
        if (entry->ip == ip)
        {
            return entry;
        }
        entry = entry->next;
    }
    
    return NULL;
}

/**
 * arp_cache_add - Add or update entry in ARP cache
 * @ip: IP address
 * @mac: MAC address
 */
void arp_cache_add(ip4_addr_t ip, const mac_addr_t mac)
{
    /* Check if entry already exists */
    struct arp_cache_entry *entry = arp_lookup(ip);
    
    if (entry)
    {
        /* Update existing entry */
        memcpy(entry->mac, mac, 6);
        entry->timestamp = 0; /* TODO: Use actual timestamp */
        LOG_INFO_FMT("ARP", "Updated ARP cache entry for IP " IP4_FMT, IP4_ARGS(ip));
        return;
    }
    
    /* Create new entry */
    entry = (struct arp_cache_entry *)kmalloc(sizeof(struct arp_cache_entry));
    if (!entry)
    {
        LOG_ERROR("ARP", "Failed to allocate ARP cache entry");
        return;
    }
    
    entry->ip = ip;
    memcpy(entry->mac, mac, 6);
    entry->timestamp = 0; /* TODO: Use actual timestamp */
    entry->next = arp_cache;
    arp_cache = entry;
    
    LOG_INFO_FMT("ARP", "Added ARP cache entry: IP " IP4_FMT " -> MAC %02x:%02x:%02x:%02x:%02x:%02x",
                 IP4_ARGS(ip),
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/**
 * arp_send_request - Send ARP request for given IP address
 * @dev: Network device to send on
 * @target_ip: IP address to resolve
 */
void arp_send_request(struct net_device *dev, ip4_addr_t target_ip)
{
    LOG_INFO_FMT("ARP", "Sending ARP request for IP " IP4_FMT, IP4_ARGS(target_ip));
    
    struct arp_header *request = (struct arp_header *)kmalloc(sizeof(struct arp_header));
    if (!request)
    {
        LOG_ERROR("ARP", "Failed to allocate ARP request");
        return;
    }
    
    memset(request, 0, sizeof(struct arp_header));
    request->hw_type = htons(ARP_HW_TYPE_ETHERNET);
    request->proto_type = htons(ARP_PROTO_TYPE_IPV4);
    request->hw_len = 6;
    request->proto_len = 4;
    request->opcode = htons(ARP_OP_REQUEST);
    
    /* Sender (us) */
    memcpy(request->sender_mac, dev->mac, 6);
    request->sender_ip = htonl(my_ip);
    
    /* Target (unknown MAC, IP to resolve) */
    memset(request->target_mac, 0, 6);
    request->target_ip = htonl(target_ip);
    
    /* Send ARP request as broadcast */
    if (net_send(dev, ETHERTYPE_ARP, broadcast_mac, request, sizeof(struct arp_header)) == 0)
    {
        LOG_INFO("ARP", "ARP Request sent");
    }
    else
    {
        LOG_ERROR("ARP", "Failed to send ARP Request");
    }
    
    kfree(request);
}

/**
 * arp_resolve - Resolve IP address to MAC address
 * @dev: Network device to use
 * @ip: IP address to resolve
 * @mac: Buffer to store MAC address (6 bytes)
 * @return: 0 on success, -1 on error
 */
int arp_resolve(struct net_device *dev, ip4_addr_t ip, mac_addr_t mac)
{
    /* Check ARP cache first */
    struct arp_cache_entry *entry = arp_lookup(ip);
    if (entry)
    {
        memcpy(mac, entry->mac, 6);
        LOG_INFO_FMT("ARP", "IP " IP4_FMT " resolved from cache", IP4_ARGS(ip));
        return 0;
    }
    
    /* Not in cache, send ARP request */
    LOG_INFO_FMT("ARP", "IP " IP4_FMT " not in cache, sending ARP request", IP4_ARGS(ip));
    arp_send_request(dev, ip);
    
    /* TODO: Wait for reply with timeout, for now just return error */
    return -1;
}

/**
 * arp_print_cache - Print all entries in ARP cache
 */
void arp_print_cache(void)
{
    struct arp_cache_entry *entry = arp_cache;
    int count = 0;
    
    LOG_INFO("ARP", "ARP Cache:");
    
    if (!entry)
    {
        LOG_INFO("ARP", "  (cache empty)");
        return;
    }
    
    while (entry)
    {
        LOG_INFO_FMT("ARP", "  %d. IP " IP4_FMT " -> MAC %02x:%02x:%02x:%02x:%02x:%02x",
                     ++count,
                     IP4_ARGS(entry->ip),
                     entry->mac[0], entry->mac[1], entry->mac[2],
                     entry->mac[3], entry->mac[4], entry->mac[5]);
        entry = entry->next;
    }
    
    LOG_INFO_FMT("ARP", "Total entries: %d", count);
}

