/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ip.c
 * Description: IPv4 protocol implementation
 */

#include "ip.h"
#include <ir0/memory/kmem.h>
#include <ir0/logging.h>
#include <drivers/serial/serial.h>
#include "arp.h"
#include <string.h>

/* IP Configuration (default values) */
ip4_addr_t ip_local_addr = 0;  /* Will be set during initialization */
ip4_addr_t ip_netmask = 0;
ip4_addr_t ip_gateway = 0;

/* IP Protocol registration */
static struct net_protocol ip_proto;

/* Temporary storage for source IP when calling upper layer handlers */
/* This is used to pass source IP to ICMP for Echo Reply */
static ip4_addr_t ip_last_src_addr = 0;

/* Broadcast IP address */
static const ip4_addr_t broadcast_ip = 0xFFFFFFFF; /* 255.255.255.255 */

/**
 * ip_checksum - Calculate IP checksum
 * @data: Pointer to data
 * @len: Length of data
 * @return: 16-bit checksum in network byte order
 */
uint16_t ip_checksum(const void *data, size_t len)
{
    const uint16_t *words = (const uint16_t *)data;
    uint32_t sum = 0;
    size_t i;

    /* Sum all 16-bit words */
    for (i = 0; i < len / 2; i++)
    {
        sum += ntohs(words[i]);
    }

    /* Handle odd byte */
    if (len & 1)
    {
        sum += ((uint8_t *)data)[len - 1] << 8;
    }

    /* Fold 32-bit sum to 16 bits */
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    /* Return one's complement */
    return htons(~sum);
}

/**
 * ip_receive_handler - Handle incoming IP packets
 * @dev: Network device that received the packet
 * @data: Pointer to IP packet data
 * @len: Length of IP packet
 * @priv: Private data (unused)
 */
void ip_receive_handler(struct net_device *dev, const void *data, 
                        size_t len, void *priv)
{
    (void)priv;

    if (len < sizeof(struct ip_header))
    {
        LOG_WARNING("IP", "Packet too short");
        return;
    }

    const struct ip_header *ip = (const struct ip_header *)data;

    /* Validate IP version */
    if (IP_VERSION(ip) != 4)
    {
        LOG_WARNING("IP", "Invalid IP version");
        return;
    }

    /* Validate header length */
    uint8_t ihl = IP_IHL(ip);
    if (ihl < 5)
    {
        LOG_WARNING("IP", "Invalid IP header length");
        return;
    }

    size_t header_len = ihl * 4;
    if (len < header_len)
    {
        LOG_WARNING("IP", "Packet shorter than header length");
        return;
    }

    /* Verify checksum */
    uint16_t received_checksum = ip->checksum;
    struct ip_header *ip_mutable = (struct ip_header *)data;
    ip_mutable->checksum = 0;
    uint16_t calculated_checksum = ip_checksum(data, header_len);
    ip_mutable->checksum = received_checksum;

    if (received_checksum != calculated_checksum)
    {
        LOG_WARNING("IP", "Checksum mismatch");
        return;
    }

    /* Check if packet is for us */
    ip4_addr_t dest_ip = ip->dest_addr;
    if (dest_ip != ip_local_addr && dest_ip != broadcast_ip)
    {
        /* Not for us, silently drop */
        return;
    }

    /* Extract protocol number */
    uint8_t protocol = ip->protocol;

    /* Debug: Log IP protocol for troubleshooting */
    extern void serial_print(const char *);
    extern void serial_print_hex32(uint32_t);
    extern char *itoa(int, char*, int);
    serial_print("[IP] proto=");
    char proto_str[8];
    itoa((int)protocol, proto_str, 10);
    serial_print(proto_str);
    serial_print(" dst=");
    uint32_t host_dst = ntohl(dest_ip);
    char ip_str[16];
    itoa((int)((host_dst >> 24) & 0xFF), ip_str, 10);
    serial_print(ip_str);
    serial_print(".");
    itoa((int)((host_dst >> 16) & 0xFF), ip_str, 10);
    serial_print(ip_str);
    serial_print(".");
    itoa((int)((host_dst >> 8) & 0xFF), ip_str, 10);
    serial_print(ip_str);
    serial_print(".");
    itoa((int)(host_dst & 0xFF), ip_str, 10);
    serial_print(ip_str);
    serial_print("\n");

    LOG_INFO_FMT("IP", "Received IP packet: protocol=%d, src=" IP4_FMT ", dest=" IP4_FMT,
                 (int)protocol,
                 IP4_ARGS(ntohl(ip->src_addr)),
                 IP4_ARGS(ntohl(dest_ip)));

    /* Store source IP for upper layer handlers (e.g., ICMP Echo Reply) */
    ip_last_src_addr = ip->src_addr;

    /* Look up protocol handler by IP protocol number */
    struct net_protocol *proto = net_find_protocol_by_ipproto(protocol);
    if (proto && proto->handler)
    {
        /* Extract payload (after IP header) */
        const void *payload = (const uint8_t *)data + header_len;
        size_t payload_len = len - header_len;

        /* Call protocol handler (ICMP, TCP, UDP, etc.) */
        proto->handler(dev, payload, payload_len, proto->priv);
    }
    else
    {
        LOG_WARNING_FMT("IP", "No handler registered for IP protocol %d", (int)protocol);
    }
}

/**
 * ip_send - Send an IP packet
 * @dev: Network device to send on
 * @dest_ip: Destination IP address
 * @protocol: IP protocol number (IPPROTO_ICMP, IPPROTO_TCP, IPPROTO_UDP)
 * @payload: Payload data
 * @len: Payload length
 * @return: 0 on success, -1 on error
 */
int ip_send(struct net_device *dev, ip4_addr_t dest_ip, uint8_t protocol, 
            const void *payload, size_t len)
{
    if (!dev || !payload)
        return -1;

    /* Allocate IP packet */
    size_t ip_header_len = sizeof(struct ip_header);
    size_t total_len = ip_header_len + len;
    
    if (total_len > dev->mtu - sizeof(struct eth_header))
    {
        LOG_ERROR("IP", "Packet too large for MTU");
        return -1;
    }

    uint8_t *ip_packet = kmalloc(total_len);
    if (!ip_packet)
        return -1;

    struct ip_header *ip = (struct ip_header *)ip_packet;

    /* Fill IP header */
    ip->version_ihl = (4 << 4) | 5;  /* Version 4, IHL 5 (20 bytes) */
    ip->tos = 0;
    ip->total_len = htons(total_len);
    ip->id = htons(0);  /* Simple implementation: no fragmentation */
    ip->flags_frag_off = htons(IP_FLAG_DF);  /* Don't Fragment */
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->checksum = 0;
    ip->src_addr = ip_local_addr;
    ip->dest_addr = dest_ip;

    /* Calculate checksum */
    ip->checksum = ip_checksum(ip, ip_header_len);

    /* Copy payload */
    memcpy(ip_packet + ip_header_len, payload, len);

    /* ROUTING: Determine if destination is on local network or needs gateway */
    ip4_addr_t next_hop_ip = dest_ip;
    
    /* Check if destination is on local network */
    if (ip_netmask != 0)
    {
        ip4_addr_t local_network = ip_local_addr & ip_netmask;
        ip4_addr_t dest_network = dest_ip & ip_netmask;
        
        if (local_network != dest_network)
        {
            /* Destination is on different network, use gateway */
            if (ip_gateway != 0)
            {
                next_hop_ip = ip_gateway;
                LOG_INFO_FMT("IP", "Routing to " IP4_FMT " via gateway " IP4_FMT,
                            IP4_ARGS(ntohl(dest_ip)), IP4_ARGS(ntohl(ip_gateway)));
            }
            else
            {
                LOG_ERROR("IP", "Destination not on local network and no gateway configured");
                kfree(ip_packet);
                return -1;
            }
        }
    }

    /* Resolve next hop MAC address using ARP */
    uint8_t dest_mac[6];
    LOG_INFO_FMT("IP", "Resolving MAC for next_hop " IP4_FMT, IP4_ARGS(ntohl(next_hop_ip)));
    if (arp_resolve(dev, next_hop_ip, dest_mac) != 0)
    {
        LOG_ERROR_FMT("IP", "Failed to resolve MAC address for " IP4_FMT " (next hop)",
                     IP4_ARGS(ntohl(next_hop_ip)));
        kfree(ip_packet);
        return -1;
    }
    LOG_INFO_FMT("IP", "MAC resolved: %02x:%02x:%02x:%02x:%02x:%02x",
                 dest_mac[0], dest_mac[1], dest_mac[2], dest_mac[3], dest_mac[4], dest_mac[5]);

    /* Send Ethernet frame with IP packet */
    /* Note: IP header still has original dest_ip, but Ethernet frame goes to next_hop MAC */
    LOG_INFO_FMT("IP", "Calling net_send: dev=%p, ethertype=0x%04x, payload_len=%d, ip_packet=%p",
                 dev, ETHERTYPE_IP, (int)total_len, ip_packet);
    int ret = net_send(dev, ETHERTYPE_IP, dest_mac, ip_packet, total_len);
    LOG_INFO_FMT("IP", "net_send returned: %d", ret);

    LOG_INFO("IP", "Freeing IP packet");
    kfree(ip_packet);
    LOG_INFO("IP", "IP packet freed, returning");
    return ret;
}

/**
 * ip_init - Initialize IP protocol
 * @return: 0 on success, -1 on error
 */
int ip_init(void)
{
    extern void arp_set_my_ip(ip4_addr_t ip);
    
    /* Set default IP configuration */
    /* QEMU user mode networking uses 10.0.2.x by default:
     * - Guest IP: 10.0.2.15
     * - Gateway: 10.0.2.2 (router virtual)
     * - DNS: 10.0.2.3
     * 
     * For other network modes (TAP, bridge), use ifconfig to set IP.
     */
    ip_local_addr = ip_make_addr(10, 0, 2, 15);  /* QEMU user mode default */
    ip_netmask = ip_make_addr(255, 255, 255, 0);
    ip_gateway = ip_make_addr(10, 0, 2, 2);  /* QEMU router virtual */
    
    /* Synchronize ARP's IP address */
    arp_set_my_ip(ip_local_addr);

    LOG_INFO_FMT("IP", "Initializing IPv4 with address " IP4_FMT, IP4_ARGS(ntohl(ip_local_addr)));

    /* Register IP protocol handler */
    memset(&ip_proto, 0, sizeof(ip_proto));
    ip_proto.name = "IP";
    ip_proto.ethertype = ETHERTYPE_IP;
    ip_proto.ipproto = 0;  /* IP itself doesn't have an IP protocol number */
    ip_proto.handler = ip_receive_handler;
    ip_proto.priv = NULL;

    if (net_register_protocol(&ip_proto) != 0)
    {
        LOG_ERROR("IP", "Failed to register IP protocol");
        return -1;
    }

    LOG_INFO("IP", "IPv4 protocol initialized");
    return 0;
}

/**
 * ip_get_last_src_addr - Get source IP from last received packet
 * This is used by upper layer protocols (e.g., ICMP) to get the source IP
 * for sending replies.
 * @return: Source IP address in network byte order
 */
ip4_addr_t ip_get_last_src_addr(void)
{
    return ip_last_src_addr;
}

