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
#include <drivers/timer/clock_system.h>
#include <string.h>

/* Macros for IP address formatting */
#define IP4_FMT "%d.%d.%d.%d"
#define IP4_ARGS(ip) \
    (int)((ip >> 24) & 0xFF), \
    (int)((ip >> 16) & 0xFF), \
    (int)((ip >> 8) & 0xFF), \
    (int)(ip & 0xFF)

/* ARP Cache timeout (in milliseconds) */
#define ARP_CACHE_TIMEOUT_MS (5 * 60 * 1000)  /* 5 minutes */

/* ARP Resolution timeout (in milliseconds) */
#define ARP_RESOLVE_TIMEOUT_MS 2000  /* 2 seconds */
#define ARP_RESOLVE_RETRIES 3        /* 3 attempts */

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
    /* Keep IPs in network byte order for comparisons and cache operations */
    ip4_addr_t sender_ip = arp->sender_ip;  /* Already in network byte order */
    ip4_addr_t target_ip = arp->target_ip;  /* Already in network byte order */
    
    LOG_INFO_FMT("ARP", "Received ARP packet: opcode=%d, sender_ip=" IP4_FMT ", target_ip=" IP4_FMT,
                 (int)opcode, IP4_ARGS(ntohl(sender_ip)), IP4_ARGS(ntohl(target_ip)));
    LOG_INFO_FMT("ARP", "Sender MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 arp->sender_mac[0], arp->sender_mac[1], arp->sender_mac[2],
                 arp->sender_mac[3], arp->sender_mac[4], arp->sender_mac[5]);
    
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
            /* my_ip is already in network byte order */
            reply->sender_ip = my_ip;
            
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
                     IP4_ARGS(ntohl(sender_ip)),
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
    /* QEMU user mode networking default: 10.0.2.15 */
    my_ip = make_ip4_addr(10, 0, 2, 15);
    
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
    uint64_t now = clock_get_uptime_milliseconds();
    struct arp_cache_entry *entry = arp_cache;
    struct arp_cache_entry *prev = NULL;
    
    while (entry)
    {
        /* Check if entry has expired */
        if (now - entry->timestamp > ARP_CACHE_TIMEOUT_MS)
        {
            /* Remove expired entry */
            struct arp_cache_entry *next = entry->next;
            if (prev)
            {
                prev->next = next;
            }
            else
            {
                arp_cache = next;
            }
            kfree(entry);
            entry = next;
            continue;
        }
        
        if (entry->ip == ip)
        {
            return entry;
        }
        
        prev = entry;
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
        entry->timestamp = clock_get_uptime_milliseconds();
        LOG_INFO_FMT("ARP", "Updated ARP cache entry for IP " IP4_FMT, IP4_ARGS(ntohl(ip)));
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
    entry->timestamp = clock_get_uptime_milliseconds();
    entry->next = arp_cache;
    arp_cache = entry;
    
    LOG_INFO_FMT("ARP", "Added ARP cache entry: IP " IP4_FMT " -> MAC %02x:%02x:%02x:%02x:%02x:%02x",
                 IP4_ARGS(ntohl(ip)),
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/**
 * arp_send_request - Send ARP request for given IP address
 * @dev: Network device to send on
 * @target_ip: IP address to resolve
 */
void arp_send_request(struct net_device *dev, ip4_addr_t target_ip)
{
    LOG_INFO_FMT("ARP", "Sending ARP request for IP " IP4_FMT, IP4_ARGS(ntohl(target_ip)));
    
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
    /* my_ip is already in network byte order from make_ip4_addr */
    request->sender_ip = my_ip;
    
    /* Target (unknown MAC, IP to resolve) */
    memset(request->target_mac, 0, 6);
    /* target_ip is already in network byte order */
    request->target_ip = target_ip;
    
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
        LOG_INFO_FMT("ARP", "IP " IP4_FMT " resolved from cache", IP4_ARGS(ntohl(ip)));
        return 0;
    }
    
    /* Not in cache, try to resolve with timeout and retries */
    LOG_INFO_FMT("ARP", "IP " IP4_FMT " not in cache, attempting resolution", IP4_ARGS(ntohl(ip)));
    
    for (int retry = 0; retry < ARP_RESOLVE_RETRIES; retry++)
    {
        if (retry > 0)
        {
            LOG_INFO_FMT("ARP", "Retry %d/%d for IP " IP4_FMT, 
                        retry, ARP_RESOLVE_RETRIES, IP4_ARGS(ntohl(ip)));
        }
        
        /* Send ARP request */
        arp_send_request(dev, ip);
        
        /* Wait for ARP reply with timeout */
        uint64_t start_time = clock_get_uptime_milliseconds();
        uint64_t timeout_ms = ARP_RESOLVE_TIMEOUT_MS;
        
        /* Log to serial only for verbose info */
        extern void serial_print(const char *);
        extern void serial_print_hex32(uint32_t);
        serial_print("[ARP] Waiting for ARP reply (timeout=");
        serial_print_hex32((uint32_t)timeout_ms);
        serial_print(" ms, attempt ");
        serial_print_hex32((uint32_t)(retry + 1));
        serial_print("/");
        serial_print_hex32((uint32_t)ARP_RESOLVE_RETRIES);
        serial_print(")\n");
        
        uint64_t last_log_time = start_time;
        int check_count = 0;
        int max_checks = (timeout_ms / 10) + 10; /* Safety limit */
        
        while (check_count < max_checks)
        {
            uint64_t current_time = clock_get_uptime_milliseconds();
            uint64_t elapsed = 0;
            
            /* Check for overflow */
            if (current_time >= start_time)
            {
                elapsed = current_time - start_time;
            }
            else
            {
                LOG_WARNING("ARP", "Timer overflow detected!");
                elapsed = timeout_ms + 1; /* Force exit */
            }
            
            /* Reduced logging: only log every 500ms to reduce VGA clutter */
            if (check_count == 0 || (current_time - last_log_time) >= 500)
            {
                /* Use serial for verbose progress logs */
                extern void serial_print(const char *);
                extern void serial_print_hex32(uint32_t);
                serial_print("[ARP] Waiting... elapsed=");
                serial_print_hex32((uint32_t)elapsed);
                serial_print(" ms, check_count=");
                serial_print_hex32((uint32_t)check_count);
                serial_print("\n");
                last_log_time = current_time;
            }
            
            /* Check timeout */
            if (elapsed >= timeout_ms)
            {
                LOG_INFO_FMT("ARP", "Timeout reached: elapsed=%d ms >= timeout=%d ms", 
                            (int)elapsed, (int)timeout_ms);
                break;
            }
            
            /* CRITICAL: Poll network card for received packets */
            /* This is necessary because interrupts may not be working properly */
            /* Include rtl8139.h at top of file for this function */
            {
                extern void rtl8139_poll(void);
                rtl8139_poll();
            }
            
            /* Check if entry appeared in cache (ARP reply received) */
            entry = arp_lookup(ip);
            if (entry)
            {
                memcpy(mac, entry->mac, 6);
                LOG_INFO_FMT("ARP", "IP " IP4_FMT " resolved after %d ms (attempt %d)",
                            IP4_ARGS(ntohl(ip)), (int)elapsed, retry + 1);
                LOG_INFO_FMT("ARP", "Resolved MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                return 0;
            }
            
            check_count++;
            
            /* Reduced logging: only log every 50 checks (every ~500ms) to reduce VGA clutter */
            if ((check_count % 50) == 0)
            {
                uint64_t now = clock_get_uptime_milliseconds();
                uint64_t elapsed_check = (now >= start_time) ? (now - start_time) : 0;
                /* Use serial only for verbose logging */
                extern void serial_print(const char *);
                extern void serial_print_hex32(uint32_t);
                serial_print("[ARP] Loop check #");
                serial_print_hex32((uint32_t)check_count);
                serial_print(", elapsed=");
                serial_print_hex32((uint32_t)elapsed_check);
                serial_print(" ms\n");
            }
            
            /* Small delay to allow interrupts to be processed */
            /* Instead of clock_sleep() which uses hlt (can block forever),
             * we do a small busy-wait loop that checks the timer periodically.
             * CRITICAL: We must enable interrupts during the delay to allow
             * timer interrupts to be processed and update the clock.
             */
            uint64_t delay_start = clock_get_uptime_milliseconds();
            uint64_t delay_target = delay_start + 10; /* 10ms delay */
            int delay_iterations = 0;
            
            /* Enable interrupts during delay to allow timer to advance */
            __asm__ volatile("sti");
            
            /* Busy-wait checking timer periodically */
            while (clock_get_uptime_milliseconds() < delay_target)
            {
                delay_iterations++;
                /* Check every ~1000 iterations to avoid excessive timer calls */
                if ((delay_iterations % 1000) == 0)
                {
                    /* If timer hasn't advanced, break to avoid infinite loop */
                    if (clock_get_uptime_milliseconds() == delay_start && delay_iterations > 10000)
                    {
                        LOG_WARNING("ARP", "Timer not advancing during delay, breaking delay loop");
                        break;
                    }
                }
                /* Small CPU pause to reduce power consumption and allow interrupts */
                __asm__ volatile("pause");
            }
            
            /* Keep interrupts enabled to allow RX interrupts to be processed */
            /* Note: We don't disable interrupts here because we need RX interrupts
             * from the network card to process ARP replies. The ARP resolution
             * loop is already protected by checking the cache entry atomically.
             */
        }
        
        uint64_t elapsed = clock_get_uptime_milliseconds() - start_time;
        LOG_WARNING_FMT("ARP", "Timeout waiting for ARP reply after %d ms (attempt %d/%d, checks=%d)",
                       (int)elapsed, retry + 1, ARP_RESOLVE_RETRIES, check_count);
        
        /* Check if we should continue with next retry */
        if (retry < ARP_RESOLVE_RETRIES - 1)
        {
            LOG_INFO_FMT("ARP", "Continuing to next retry... (retry %d/%d)", 
                        retry + 2, ARP_RESOLVE_RETRIES);
        }
    }
    
    LOG_ERROR_FMT("ARP", "Failed to resolve IP " IP4_FMT " after %d attempts",
                 IP4_ARGS(ntohl(ip)), ARP_RESOLVE_RETRIES);
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
                     IP4_ARGS(ntohl(entry->ip)),
                     entry->mac[0], entry->mac[1], entry->mac[2],
                     entry->mac[3], entry->mac[4], entry->mac[5]);
        entry = entry->next;
    }
    
    LOG_INFO_FMT("ARP", "Total entries: %d", count);
}

/**
 * arp_set_my_ip - Update ARP's IP address (synchronize with IP layer)
 * @ip: New IP address in network byte order
 */
void arp_set_my_ip(ip4_addr_t ip)
{
    my_ip = ip;
    LOG_INFO_FMT("ARP", "Updated ARP IP address to " IP4_FMT, IP4_ARGS(ntohl(ip)));
}

