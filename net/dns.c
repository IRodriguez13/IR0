/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: dns.c
 * Description: DNS (Domain Name System) client implementation
 *
 * DNS allows resolving human-readable domain names (e.g., "www.example.com")
 * to IP addresses. This is a basic DNS client implementation that:
 * - Sends DNS queries over UDP
 * - Parses DNS responses to extract A records (IPv4 addresses)
 * - Supports recursive queries (RD flag)
 *
 * The implementation is minimal but functional for basic DNS resolution needed
 * for ping and other network utilities.
 */

#include "dns.h"
#include "udp.h"
#include "ip.h"
#include <ir0/kmem.h>
#include <ir0/logging.h>
#include <ir0/serial_io.h>
#include <ir0/clock.h>
#include <arch/common/arch_portable.h>
#include <stdbool.h>
#include <string.h>

/* DNS Header Structure (RFC 1035) */
struct dns_header {
    uint16_t id;        /* Query identifier */
    uint16_t flags;     /* Query flags (QR, OPCODE, AA, TC, RD, RA, RCODE) */
    uint16_t qdcount;   /* Number of questions */
    uint16_t ancount;   /* Number of answer records */
    uint16_t nscount;   /* Number of authority records */
    uint16_t arcount;   /* Number of additional records */
} __attribute__((packed));

/* DNS Question Section */
struct dns_question {
    /* QNAME: domain name (variable length, null-terminated labels) */
    /* QTYPE: 16-bit query type */
    /* QCLASS: 16-bit query class */
} __attribute__((packed));

/* DNS Resource Record */
struct dns_rr {
    /* NAME: domain name (variable length, may be compressed) */
    /* TYPE: 16-bit record type */
    /* CLASS: 16-bit record class */
    /* TTL: 32-bit time to live */
    /* RDLENGTH: 16-bit data length */
    /* RDATA: record data (variable length) */
} __attribute__((packed));

/* DNS query/response state */
struct dns_query_state {
    uint16_t id;
    ip4_addr_t result;
    uint64_t timestamp;
    bool resolved;
    struct dns_query_state *next;
};

static struct dns_query_state *pending_queries = NULL;
static uint16_t dns_query_id_counter = 1;
static struct net_device *dns_net_dev = NULL;
static uint16_t dns_client_port = 5353;  /* Ephemeral port for DNS queries */
static bool dns_handler_registered = false;
static ip4_addr_t dns_default_server = 0;

static inline uint64_t dns_irq_save(void)
{
#if defined(__x86_64__) || defined(__i386__)
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
#else
    arch_disable_interrupts();
    return 0;
#endif
}

static inline void dns_irq_restore(uint64_t flags)
{
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
#else
    (void)flags;
    arch_enable_interrupts();
#endif
}

static struct dns_query_state *dns_pending_find_locked(uint16_t id)
{
    struct dns_query_state *query = pending_queries;
    while (query)
    {
        if (query->id == id)
            return query;
        query = query->next;
    }
    return NULL;
}

static void dns_pending_add(struct dns_query_state *query)
{
    uint64_t flags;

    if (!query)
        return;

    flags = dns_irq_save();
    query->next = pending_queries;
    pending_queries = query;
    dns_irq_restore(flags);
}

static void dns_pending_remove_locked(struct dns_query_state *query)
{
    struct dns_query_state *prev;

    if (!query)
        return;

    if (pending_queries == query)
    {
        pending_queries = query->next;
        query->next = NULL;
        return;
    }

    prev = pending_queries;
    while (prev && prev->next != query)
        prev = prev->next;
    if (prev)
    {
        prev->next = query->next;
        query->next = NULL;
    }
}

static void dns_pending_remove(struct dns_query_state *query)
{
    uint64_t flags = dns_irq_save();
    dns_pending_remove_locked(query);
    dns_irq_restore(flags);
}

/* Helper: Encode domain name for DNS (convert "example.com" to length-prefixed format) */
static size_t dns_encode_name(const char *domain, uint8_t *buf, size_t buf_len)
{
    size_t pos = 0;
    const char *start = domain;
    
    while (*domain && pos < buf_len - 1)
    {
        if (*domain == '.')
        {
            /* Write length of label */
            if (pos + (domain - start) + 1 > buf_len)
                return 0;
            
            buf[pos++] = (uint8_t)(domain - start);
            
            /* Copy label */
            for (const char *p = start; p < domain; p++)
            {
                buf[pos++] = *p;
            }
            
            start = domain + 1;
        }
        domain++;
    }
    
    /* Write last label */
    if (pos + (domain - start) + 2 > buf_len)
        return 0;
    
    buf[pos++] = (uint8_t)(domain - start);
    for (const char *p = start; p < domain; p++)
    {
        buf[pos++] = *p;
    }
    
    /* Null terminator */
    buf[pos++] = 0;
    
    return pos;
}

/* Helper: Decode DNS name from response (handle compression pointers) */
static const uint8_t *dns_decode_name(const uint8_t *data, const uint8_t *buf_start,
                                      size_t buf_len, char *out, size_t out_len)
{
    size_t out_pos = 0;
    const uint8_t *pkt_end = buf_start + buf_len;
    const uint8_t *ptr = data;
    const uint8_t *consumed = NULL;
    int jumps = 0;

    if (!data || !buf_start || !out || out_len == 0 || data >= pkt_end)
        return NULL;

    while (ptr < pkt_end && jumps < 32)
    {
        uint8_t c = *ptr;
        if (c == 0)
        {
            if (!consumed)
                consumed = ptr + 1;
            out[out_pos] = '\0';
            return consumed;
        }

        if ((*ptr & 0xC0) == 0xC0)
        {
            if (ptr + 1 >= buf_start + buf_len)
                return NULL;
            uint16_t offset = (uint16_t)(((*ptr & 0x3F) << 8) | ptr[1]);
            if (offset >= buf_len)
                return NULL;
            if (!consumed)
                consumed = ptr + 2;
            ptr = buf_start + offset;
            jumps++;
            continue;
        }

        if ((c & 0xC0) != 0)
            return NULL;

        uint8_t label_len = c;
        ptr++;
        if (label_len > 63 || ptr + label_len > pkt_end)
            return NULL;
        if (out_pos + (out_pos > 0 ? 1 : 0) + label_len + 1 > out_len)
            return NULL;

        if (out_pos > 0)
            out[out_pos++] = '.';

        for (uint8_t i = 0; i < label_len; i++)
            out[out_pos++] = *ptr++;
    }

    return NULL;
}

/* DNS response handler */
static void dns_response_handler(struct net_device *dev, ip4_addr_t src_ip,
                                 uint16_t src_port, const void *data, size_t len)
{
    (void)dev;
    (void)src_port;
    
    LOG_INFO_FMT("DNS", "DNS response received from " IP4_FMT " port %d, len=%d", 
                 IP4_ARGS(ntohl(src_ip)), (int)src_port, (int)len);
    
    if (len < sizeof(struct dns_header))
    {
        LOG_WARNING("DNS", "DNS response too short");
        return;
    }
    
    const struct dns_header *header = (const struct dns_header *)data;
    uint16_t id = ntohs(header->id);
    uint16_t flags = ntohs(header->flags);
    uint16_t qdcount = ntohs(header->qdcount);
    uint16_t ancount = ntohs(header->ancount);
    
    /* Check if QR bit is set (response) */
    if (!(flags & 0x8000))
    {
        LOG_WARNING("DNS", "DNS packet is not a response");
        return;
    }
    
    /* Check response code */
    uint8_t rcode = flags & 0x0F;
    if (rcode != DNS_RCODE_NOERROR)
    {
        LOG_WARNING_FMT("DNS", "DNS error response: RCODE=%d", (int)rcode);
        return;
    }
    
    LOG_INFO_FMT("DNS", "DNS response: id=%d, answers=%d", (int)id, (int)ancount);
    
    /* Parse response to find A record */
    const uint8_t *ptr = (const uint8_t *)data + sizeof(struct dns_header);
    ip4_addr_t resolved_ip = 0;
    bool found_a_record = false;
    
    /* Skip question section */
    const uint8_t *pkt_end = (const uint8_t *)data + len;
    for (uint16_t i = 0; i < qdcount; i++)
    {
        char qname[256];
        const uint8_t *name_end = dns_decode_name(ptr, (const uint8_t *)data, len, qname, sizeof(qname));
        if (!name_end)
            return;
        ptr = name_end;
        
        /* Skip QTYPE and QCLASS */
        if (ptr + 4 > pkt_end)
            return;
        ptr += 4;
    }
    
    /* Parse answer section */
    for (uint16_t i = 0; i < ancount; i++)
    {
        if (ptr >= pkt_end)
            break;
        
        /* Skip NAME (may be compressed) */
        char name[256];
        const uint8_t *name_end = dns_decode_name(ptr, (const uint8_t *)data, len, name, sizeof(name));
        if (!name_end)
            break;
        ptr = name_end;
        
        /* Read TYPE, CLASS, TTL, RDLENGTH */
        if (ptr + 10 > pkt_end)
            break;
        
        uint16_t type = ntohs(*(uint16_t *)ptr);
        ptr += 2;
        uint16_t class = ntohs(*(uint16_t *)ptr);
        ptr += 2;
        ptr += 4;  /* Skip TTL */
        uint16_t rdlength = ntohs(*(uint16_t *)ptr);
        ptr += 2;
        
        /* If A record, extract IP address */
        if (type == DNS_TYPE_A && class == DNS_CLASS_IN && rdlength == 4)
        {
            if (ptr + 4 <= pkt_end)
            {
                memcpy(&resolved_ip, ptr, 4);
                found_a_record = true;
                LOG_INFO_FMT("DNS", "Resolved %s to " IP4_FMT, name, IP4_ARGS(ntohl(resolved_ip)));
                break;
            }
        }
        
        /* Skip RDATA */
        if (ptr + rdlength > pkt_end)
            break;
        ptr += rdlength;
    }
    
    if (!found_a_record)
    {
        LOG_WARNING("DNS", "No A record found in DNS response");
        return;
    }

    {
        uint64_t flags = dns_irq_save();
        struct dns_query_state *query = dns_pending_find_locked(id);
        if (!query)
        {
            dns_irq_restore(flags);
            LOG_WARNING_FMT("DNS", "DNS response id=%d not matched", (int)id);
            return;
        }
        query->result = resolved_ip;
        query->resolved = true;
        dns_irq_restore(flags);
    }
}

/**
 * dns_resolve - Resolve a domain name to an IP address
 * @domain_name: Domain name to resolve (e.g., "www.example.com")
 * @dns_server_ip: DNS server IP address (e.g., 8.8.8.8 or 10.0.2.3 for QEMU)
 * @return: IP address in network byte order, or 0 on error
 */
ip4_addr_t dns_resolve(const char *domain_name, ip4_addr_t dns_server_ip)
{
    if (!domain_name || !dns_net_dev)
    {
        LOG_ERROR("DNS", "DNS not initialized or invalid parameters");
        return 0;
    }

    if (dns_server_ip == 0)
    {
        dns_server_ip = dns_get_default_server();
    }
    
    /* Get network device */
    struct net_device *dev = net_get_devices();
    if (!dev)
    {
        LOG_ERROR("DNS", "No network device available");
        return 0;
    }
    
    /* Build DNS query */
    uint8_t query_buf[DNS_MAX_QUERY_LEN];
    size_t query_len = 0;
    
    /* DNS Header */
    struct dns_header *header = (struct dns_header *)query_buf;
    uint16_t query_id;
    {
        uint64_t flags = dns_irq_save();
        query_id = dns_query_id_counter++;
        dns_irq_restore(flags);
    }
    header->id = htons(query_id);
    header->flags = htons(DNS_FLAG_RD);  /* Recursion Desired */
    header->qdcount = htons(1);          /* One question */
    header->ancount = 0;
    header->nscount = 0;
    header->arcount = 0;
    query_len = sizeof(struct dns_header);
    
    /* Question Section */
    size_t name_len = dns_encode_name(domain_name, query_buf + query_len, 
                                      DNS_MAX_QUERY_LEN - query_len);
    if (name_len == 0)
    {
        LOG_ERROR("DNS", "Failed to encode domain name");
        return 0;
    }
    query_len += name_len;
    
    /* QTYPE: A record */
    if (query_len + 4 > DNS_MAX_QUERY_LEN)
    {
        LOG_ERROR("DNS", "DNS query too large");
        return 0;
    }
    *(uint16_t *)(query_buf + query_len) = htons(DNS_TYPE_A);
    query_len += 2;
    *(uint16_t *)(query_buf + query_len) = htons(DNS_CLASS_IN);
    query_len += 2;
    
    /* Create query state */
    struct dns_query_state *query = kmalloc(sizeof(struct dns_query_state));
    if (!query)
        return 0;
    
    query->id = query_id;
    query->result = 0;
    query->timestamp = clock_get_uptime_milliseconds();
    query->resolved = false;
    query->next = NULL;
    dns_pending_add(query);
    
    LOG_INFO_FMT("DNS", "Resolving %s via DNS server " IP4_FMT, 
                 domain_name, IP4_ARGS(ntohl(dns_server_ip)));
    
    /* Register handler for DNS responses if not already registered */
    if (!dns_handler_registered)
    {
        uint64_t flags = dns_irq_save();
        if (!dns_handler_registered)
        {
            udp_register_handler(dns_client_port, dns_response_handler);
            dns_handler_registered = true;
        }
        dns_irq_restore(flags);
    }
    
    /* Send DNS query */
    LOG_INFO_FMT("DNS", "Sending DNS query to " IP4_FMT " port %d", 
                 IP4_ARGS(ntohl(dns_server_ip)), DNS_PORT);
    int ret = udp_send(dev, dns_server_ip, dns_client_port, DNS_PORT, query_buf, query_len);
    if (ret != 0)
    {
        LOG_ERROR("DNS", "Failed to send DNS query");
        /* Remove from pending */
        dns_pending_remove(query);
        kfree(query);
        return 0;
    }
    LOG_INFO("DNS", "DNS query sent successfully, waiting for response...");
    
    /* Wait for response with timeout. We poll the network card periodically
     * to check for incoming DNS responses. The polling is necessary because
     * interrupts might not be working reliably.
     */
    uint64_t start_time = clock_get_uptime_milliseconds();
    uint64_t timeout_ms = DNS_DEFAULT_TIMEOUT_MS;
    uint64_t last_poll_time = start_time;
    uint64_t last_log_time = start_time;
    const uint64_t poll_interval_ms = 10;  /* Poll every 10ms to balance responsiveness and CPU usage */
    const uint64_t log_interval_ms = 1000;  /* Log every 1 second to show progress */
    uint64_t stagnant_loops = 0;
    uint64_t last_time_sample = start_time;
    const uint64_t max_stagnant_loops = 50000000ULL;
    
    while (1)
    {
        uint64_t current_time = clock_get_uptime_milliseconds();
        uint64_t elapsed = 0;
        
        /* Handle timer overflow */
        if (current_time >= start_time)
        {
            elapsed = current_time - start_time;
        }
        else
        {
            /* Timer overflow - assume timeout */
            elapsed = timeout_ms + 1;
        }
        
        /* Check timeout */
        if (elapsed >= timeout_ms)
        {
            LOG_WARNING_FMT("DNS", "DNS resolution timeout for %s after %d ms", 
                           domain_name, (int)timeout_ms);
            break;
        }

        /*
         * Failsafe: only trigger if timer value does not advance for a very
         * long period, to avoid false positives under normal short loops.
         */
        if (current_time == last_time_sample)
            stagnant_loops++;
        else
        {
            stagnant_loops = 0;
            last_time_sample = current_time;
        }
        if (stagnant_loops >= max_stagnant_loops)
        {
            LOG_WARNING_FMT("DNS", "DNS resolution aborted by stagnant-time guard for %s", domain_name);
            break;
        }
        
        /* Log progress every second */
        if (current_time - last_log_time >= log_interval_ms)
        {
            LOG_INFO_FMT("DNS", "Still waiting for DNS response for %s (%d ms elapsed, timeout in %d ms)",
                        domain_name, (int)elapsed, (int)(timeout_ms - elapsed));
            last_log_time = current_time;
        }
        
        /* Poll network stack periodically (every poll_interval_ms)
         * This processes packets through the full stack: Ethernet -> IP -> UDP -> DNS
         */
        if (current_time - last_poll_time >= poll_interval_ms)
        {
            net_poll();
            last_poll_time = current_time;
        }
        
        /* Check if DNS response arrived */
        {
            bool resolved_now = false;
            ip4_addr_t resolved_ip = 0;
            uint64_t flags = dns_irq_save();
            if (query->resolved)
            {
                resolved_now = true;
                resolved_ip = query->result;
                dns_pending_remove_locked(query);
            }
            dns_irq_restore(flags);

            if (resolved_now)
            {
                uint64_t rtt = current_time - start_time;
                
                LOG_INFO_FMT("DNS", "DNS resolution successful for %s (RTT: %d ms)", 
                            domain_name, (int)rtt);
                
                kfree(query);
                
                return resolved_ip;
            }
        }
        
        /* Small delay to prevent busy-waiting */
        for (volatile int i = 0; i < 1000; i++);  /* Reduced delay since we have poll_interval */
    }
    
    LOG_WARNING_FMT("DNS", "DNS resolution timeout for %s", domain_name);
    
    /* Remove from pending */
    dns_pending_remove(query);
    kfree(query);
    
    return 0;
}

void dns_set_default_server(ip4_addr_t dns_server_ip)
{
    uint64_t flags = dns_irq_save();
    dns_default_server = dns_server_ip;
    dns_irq_restore(flags);

    if (dns_server_ip != 0)
    {
        LOG_INFO_FMT("DNS", "Default DNS server set to " IP4_FMT, IP4_ARGS(ntohl(dns_server_ip)));
    }
}

ip4_addr_t dns_get_default_server(void)
{
    ip4_addr_t server;
    uint64_t flags = dns_irq_save();
    server = dns_default_server;
    dns_irq_restore(flags);
    return server;
}

/**
 * dns_init - Initialize DNS client
 * @return: 0 on success, -1 on error
 */
int dns_init(void)
{
    LOG_INFO("DNS", "Initializing DNS client");
    
    /* Register DNS response handler on UDP port 5353 (we use ephemeral port for responses) */
    /* Actually, DNS responses come to the same port we sent from, but since we
     * use dynamic source ports, we need to handle responses differently.
     * For simplicity, we'll register a handler on a fixed port and use that.
     */
    
    /* Get network device */
    struct net_device *dev = net_get_devices();
    if (!dev)
    {
        LOG_WARNING("DNS", "No network device available, DNS will work when device is registered");
    }
    else
    {
        dns_net_dev = dev;
    }

    /* Default to QEMU user-mode resolver; DHCP can override at runtime. */
    dns_set_default_server(htonl((10U << 24) | (0U << 16) | (2U << 8) | 3U));
    
    /* Note: DNS uses the same source port for queries and responses, but our
     * implementation is simplified. In practice, we'd need to match responses
     * by query ID and handle dynamic source ports properly.
     */
    
    LOG_INFO("DNS", "DNS client initialized");
    return 0;
}

