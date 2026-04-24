/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: udp.c
 * Description: UDP (User Datagram Protocol) implementation
 *
 * UDP is a simple, connectionless transport protocol that provides datagram
 * delivery service. Unlike TCP, UDP doesn't provide reliability, ordering,
 * or flow control. It's commonly used for DNS, DHCP, and real-time applications
 * where speed is more important than reliability.
 *
 * Our implementation provides basic UDP send/receive functionality to support
 * DNS queries and other simple UDP-based services.
 */

#include "udp.h"
#include "ip.h"
#include <ir0/kmem.h>
#include <ir0/logging.h>
#include <drivers/serial/serial.h>
#include <arch/common/arch_portable.h>
#include <string.h>

/* UDP Protocol registration */
static struct net_protocol udp_proto;

/* UDP receive callbacks: map port -> handler function */
struct udp_port_handler {
    uint16_t port;
    void (*handler)(struct net_device *dev, ip4_addr_t src_ip, uint16_t src_port,
                    const void *data, size_t len);
    struct udp_port_handler *next;
};

static struct udp_port_handler *udp_handlers = NULL;

static inline uint64_t udp_irq_save(void)
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

static inline void udp_irq_restore(uint64_t flags)
{
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
#else
    (void)flags;
    arch_enable_interrupts();
#endif
}

/**
 * udp_checksum - Calculate UDP checksum (pseudo-header + UDP header + data)
 *
 * UDP checksum includes a pseudo-header with IP source/dest addresses, protocol
 * number, and UDP length. This provides end-to-end error detection across the
 * IP and UDP layers. The checksum is optional (0 means no checksum), but we
 * calculate it for better reliability.
 *
 * @data: Pointer to UDP packet (header + payload)
 * @len: Length of UDP packet
 * @src_ip: Source IP address (for pseudo-header)
 * @dest_ip: Destination IP address (for pseudo-header)
 * @return: 16-bit checksum in network byte order
 */
uint16_t udp_checksum(const void *data, size_t len, ip4_addr_t src_ip, ip4_addr_t dest_ip)
{
    /* Build pseudo-header for checksum calculation */
    struct {
        ip4_addr_t src;
        ip4_addr_t dst;
        uint8_t zero;
        uint8_t protocol;
        uint16_t length;
    } __attribute__((packed)) pseudo_header;
    
    pseudo_header.src = src_ip;
    pseudo_header.dst = dest_ip;
    pseudo_header.zero = 0;
    pseudo_header.protocol = IPPROTO_UDP;
    pseudo_header.length = htons((uint16_t)len);
    
    /* Sum pseudo-header (copy to aligned buffer to avoid -Waddress-of-packed-member) */
    uint16_t aligned_pseudo[sizeof(pseudo_header) / 2];
    memcpy(aligned_pseudo, &pseudo_header, sizeof(pseudo_header));
    uint32_t sum = 0;
    for (size_t i = 0; i < sizeof(aligned_pseudo) / sizeof(aligned_pseudo[0]); i++)
    {
        sum += ntohs(aligned_pseudo[i]);
    }
    
    /* Sum UDP packet */
    const uint16_t *words = (const uint16_t *)data;
    for (size_t i = 0; i < len / 2; i++)
    {
        sum += ntohs(words[i]);
    }
    
    /* Handle odd byte */
    if (len & 1)
    {
        sum += ((uint8_t *)data)[len - 1] << 8;
    }
    
    /* Fold to 16 bits */
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return htons(~sum);
}

/**
 * udp_receive_handler - Process incoming UDP packets
 *
 * UDP is a connectionless protocol - packets arrive addressed to specific ports.
 * We maintain a list of port handlers that applications can register to receive
 * packets on specific ports. When a UDP packet arrives, we look up the handler
 * for the destination port and call it with the packet data.
 *
 * @dev: Network device that received the packet
 * @data: Pointer to UDP packet (after IP header)
 * @len: Length of UDP packet
 * @priv: Private data (unused, provided for protocol handler signature compatibility)
 */
void udp_receive_handler(struct net_device *dev, const void *data, size_t len, void *priv)
{
    const struct ip_rx_context *rx_ctx = (const struct ip_rx_context *)priv;
    
    if (len < sizeof(struct udp_header))
    {
        LOG_WARNING("UDP", "Packet too short");
        return;
    }
    
    const struct udp_header *udp = (const struct udp_header *)data;
    uint16_t src_port = ntohs(udp->src_port);
    uint16_t dest_port = ntohs(udp->dest_port);
    uint16_t packet_len = ntohs(udp->length);
    
    /* Validate packet length */
    if (packet_len > len || packet_len < sizeof(struct udp_header))
    {
        LOG_WARNING("UDP", "Invalid UDP packet length");
        return;
    }
    
    /* Get source IP from IP layer */
    ip4_addr_t src_ip = rx_ctx ? rx_ctx->src_addr : 0;
    
    /* Verify checksum without relying on mutating the RX buffer (copy when possible) */
    if (udp->checksum != 0)
    {
        uint16_t received_checksum = udp->checksum;
        ip4_addr_t dest_ip = rx_ctx ? rx_ctx->dest_addr : 0;
        uint16_t calculated_checksum;
        uint8_t *udp_scratch = kmalloc(packet_len);

        if (!udp_scratch)
        {
            LOG_WARNING("UDP", "No memory to verify checksum");
            return;
        }
        memcpy(udp_scratch, data, packet_len);
        ((struct udp_header *)udp_scratch)->checksum = 0;
        calculated_checksum = udp_checksum(udp_scratch, packet_len, src_ip, dest_ip);
        kfree(udp_scratch);

        if (received_checksum != calculated_checksum)
        {
            LOG_WARNING("UDP", "Checksum mismatch");
            return;
        }
    }
    
    LOG_INFO_FMT("UDP", "Received UDP packet: src=" IP4_FMT ":%d, dest=%d, len=%d",
                 IP4_ARGS(ntohl(src_ip)), (int)src_port, (int)dest_port, (int)packet_len);
    
    /* Look up handler for destination port */
    struct udp_port_handler *handler = NULL;
    void (*handler_fn)(struct net_device *, ip4_addr_t, uint16_t, const void *, size_t) = NULL;
    uint64_t irq_flags = udp_irq_save();
    handler = udp_handlers;
    while (handler)
    {
        if (handler->port == dest_port)
        {
            handler_fn = handler->handler;
            break;
        }
        handler = handler->next;
    }

    udp_irq_restore(irq_flags);
    if (handler_fn)
    {
        /* Extract payload (after UDP header) */
        const void *payload = (const uint8_t *)data + sizeof(struct udp_header);
        size_t payload_len = packet_len - sizeof(struct udp_header);
        handler_fn(dev, src_ip, src_port, payload, payload_len);
        return;
    }

    LOG_INFO_FMT("UDP", "No handler registered for port %d", (int)dest_port);
}

/**
 * udp_send - Send a UDP datagram
 *
 * UDP is connectionless - we just send the datagram to the specified destination.
 * The function encapsulates the data in a UDP header, calculates checksum,
 * and sends it via the IP layer.
 *
 * @dev: Network device to send on
 * @dest_ip: Destination IP address
 * @src_port: Source port (typically > 1024 for clients)
 * @dest_port: Destination port (e.g., 53 for DNS)
 * @data: Payload data
 * @len: Payload length
 * @return: 0 on success, -1 on error
 */
int udp_send(struct net_device *dev, ip4_addr_t dest_ip, uint16_t src_port,
             uint16_t dest_port, const void *data, size_t len)
{
    if (!dev || !data)
        return -1;
    
    /* Allocate UDP packet */
    size_t udp_header_len = sizeof(struct udp_header);
    size_t total_len = udp_header_len + len;
    
    uint8_t *udp_packet = kmalloc(total_len);
    if (!udp_packet)
        return -1;
    
    struct udp_header *udp = (struct udp_header *)udp_packet;
    
    /* Fill UDP header */
    udp->src_port = htons(src_port);
    udp->dest_port = htons(dest_port);
    udp->length = htons(total_len);
    udp->checksum = 0;  /* Zero for checksum calculation */
    
    /* Copy payload */
    memcpy(udp_packet + udp_header_len, data, len);
    
    /* Calculate checksum (requires source IP) */
    ip4_addr_t src_ip = ip_local_addr;
    extern int arp_get_interface_ip(struct net_device *dev, ip4_addr_t *ip_out);
    ip4_addr_t interface_ip;
    if (arp_get_interface_ip(dev, &interface_ip) == 0)
    {
        src_ip = interface_ip;
    }
    
    udp->checksum = udp_checksum(udp_packet, total_len, src_ip, dest_ip);
    
    LOG_INFO_FMT("UDP", "Sending UDP packet: dest=" IP4_FMT ":%d, len=%d",
                 IP4_ARGS(ntohl(dest_ip)), (int)dest_port, (int)total_len);
    
    /* Send via IP layer */
    int ret = ip_send(dev, dest_ip, IPPROTO_UDP, udp_packet, total_len);
    
    kfree(udp_packet);
    return ret;
}

/**
 * udp_register_handler - Register a handler for a UDP port
 * @port: UDP port number
 * @handler: Handler function to call when packets arrive on this port
 */
void udp_register_handler(uint16_t port, void (*handler)(struct net_device *dev, ip4_addr_t src_ip,
                                                          uint16_t src_port, const void *data, size_t len))
{
    uint64_t irq_flags = udp_irq_save();

    /* Check if handler already exists for this port */
    struct udp_port_handler *existing = udp_handlers;
    while (existing)
    {
        if (existing->port == port)
        {
            /* Update existing handler */
            existing->handler = handler;
            udp_irq_restore(irq_flags);
            LOG_INFO_FMT("UDP", "Updated handler for port %d", (int)port);
            return;
        }
        existing = existing->next;
    }
    
    /* Create new handler entry */
    struct udp_port_handler *new_handler = kmalloc(sizeof(struct udp_port_handler));
    if (!new_handler)
    {
        udp_irq_restore(irq_flags);
        return;
    }
    
    new_handler->port = port;
    new_handler->handler = handler;
    new_handler->next = udp_handlers;
    udp_handlers = new_handler;
    udp_irq_restore(irq_flags);
    LOG_INFO_FMT("UDP", "Registered handler for UDP port %d", (int)port);
}

/**
 * udp_init - Initialize UDP protocol
 * @return: 0 on success, -1 on error
 */
int udp_init(void)
{
    LOG_INFO("UDP", "Initializing UDP protocol");
    
    /* Register UDP protocol handler */
    memset(&udp_proto, 0, sizeof(udp_proto));
    udp_proto.name = "UDP";
    udp_proto.ethertype = 0;  /* UDP doesn't have an EtherType */
    udp_proto.ipproto = IPPROTO_UDP;
    udp_proto.handler = udp_receive_handler;
    udp_proto.priv = NULL;
    
    if (net_register_protocol(&udp_proto) != 0)
    {
        LOG_ERROR("UDP", "Failed to register UDP protocol");
        return -1;
    }
    
    LOG_INFO("UDP", "UDP protocol initialized");
    return 0;
}

