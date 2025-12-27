/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: icmp.c
 * Description: ICMP (Internet Control Message Protocol) implementation
 */

#include "icmp.h"
#include "ip.h"
#include <ir0/memory/kmem.h>
#include <ir0/logging.h>
#include <drivers/serial/serial.h>
#include <string.h>

/* Macros for IP address formatting (matching ARP/IP) */
#define IP4_FMT "%d.%d.%d.%d"
#define IP4_ARGS(ip) \
    (int)((ip >> 24) & 0xFF), \
    (int)((ip >> 16) & 0xFF), \
    (int)((ip >> 8) & 0xFF), \
    (int)(ip & 0xFF)

/* ICMP Protocol registration */
static struct net_protocol icmp_proto;

/**
 * icmp_checksum - Calculate ICMP checksum
 * @data: Pointer to ICMP packet data
 * @len: Length of ICMP packet
 * @return: 16-bit checksum in network byte order
 */
uint16_t icmp_checksum(const void *data, size_t len)
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
 * icmp_receive_handler - Handle incoming ICMP packets
 * @dev: Network device that received the packet
 * @data: Pointer to ICMP packet data
 * @len: Length of ICMP packet
 * @priv: Private data (unused)
 */
void icmp_receive_handler(struct net_device *dev, const void *data, 
                           size_t len, void *priv)
{
    (void)priv;

    if (len < sizeof(struct icmp_header))
    {
        LOG_WARNING("ICMP", "Packet too short");
        return;
    }

    const struct icmp_header *icmp = (const struct icmp_header *)data;

    /* Verify checksum */
    uint16_t received_checksum = icmp->checksum;
    struct icmp_header *icmp_mutable = (struct icmp_header *)data;
    icmp_mutable->checksum = 0;
    uint16_t calculated_checksum = icmp_checksum(data, len);
    icmp_mutable->checksum = received_checksum;

    if (received_checksum != calculated_checksum)
    {
        LOG_WARNING("ICMP", "Checksum mismatch");
        return;
    }

    uint8_t type = icmp->type;
    uint8_t code = icmp->code;

    LOG_INFO_FMT("ICMP", "Received ICMP packet: type=%d, code=%d", 
                 (int)type, (int)code);

    switch (type)
    {
        case ICMP_TYPE_ECHO_REQUEST:
        {
            LOG_INFO("ICMP", "Echo Request received, sending Echo Reply");

            /* Allocate reply packet */
            uint8_t *reply = kmalloc(len);
            if (!reply)
            {
                LOG_ERROR("ICMP", "Failed to allocate memory for ICMP reply");
                return;
            }

            /* Copy original packet */
            memcpy(reply, data, len);

            struct icmp_header *reply_icmp = (struct icmp_header *)reply;

            /* Change type to Echo Reply */
            reply_icmp->type = ICMP_TYPE_ECHO_REPLY;
            reply_icmp->code = 0;
            reply_icmp->checksum = 0;

            /* Recalculate checksum */
            reply_icmp->checksum = icmp_checksum(reply, len);

            /* Get source IP from IP layer */
            ip4_addr_t src_ip = ip_get_last_src_addr();
            if (src_ip == 0)
            {
                LOG_WARNING("ICMP", "Cannot send Echo Reply: source IP not available");
                kfree(reply);
                return;
            }

            /* Send reply via IP layer */
            int ret = ip_send(dev, src_ip, IPPROTO_ICMP, reply, len);
            if (ret != 0)
            {
                LOG_ERROR("ICMP", "Failed to send ICMP Echo Reply");
            }
            else
            {
                LOG_INFO("ICMP", "ICMP Echo Reply sent");
            }

            kfree(reply);
            break;
        }

        case ICMP_TYPE_ECHO_REPLY:
        {
            LOG_INFO_FMT("ICMP", "Echo Reply received: id=%d, seq=%d",
                         (int)ntohs(icmp->un.echo.id),
                         (int)ntohs(icmp->un.echo.seq));
            break;
        }

        case ICMP_TYPE_DEST_UNREACH:
        {
            LOG_WARNING_FMT("ICMP", "Destination Unreachable: code=%d", (int)code);
            break;
        }

        case ICMP_TYPE_TIME_EXCEEDED:
        {
            LOG_WARNING_FMT("ICMP", "Time Exceeded: code=%d", (int)code);
            break;
        }

        default:
        {
            LOG_INFO_FMT("ICMP", "Unhandled ICMP message type: %d", (int)type);
            break;
        }
    }
}

/**
 * icmp_send_echo_request - Send an ICMP Echo Request (ping)
 * @dev: Network device to send on
 * @dest_ip: Destination IP address
 * @id: Echo identifier
 * @seq: Echo sequence number
 * @data: Optional data payload
 * @len: Data payload length
 * @return: 0 on success, -1 on error
 */
int icmp_send_echo_request(struct net_device *dev, ip4_addr_t dest_ip, 
                           uint16_t id, uint16_t seq, const void *data, size_t len)
{
    if (!dev)
        return -1;

    /* ICMP Echo Request structure */
    size_t icmp_len = sizeof(struct icmp_header) + len;
    uint8_t *icmp_packet = kmalloc(icmp_len);
    if (!icmp_packet)
        return -1;

    struct icmp_header *icmp = (struct icmp_header *)icmp_packet;

    /* Fill ICMP header */
    icmp->type = ICMP_TYPE_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->un.echo.id = htons(id);
    icmp->un.echo.seq = htons(seq);

    /* Copy payload data if provided */
    if (data && len > 0)
    {
        memcpy(icmp_packet + sizeof(struct icmp_header), data, len);
    }

    /* Calculate checksum */
    icmp->checksum = icmp_checksum(icmp_packet, icmp_len);

    LOG_INFO_FMT("ICMP", "Sending Echo Request to " IP4_FMT " (id=%d, seq=%d)",
                 IP4_ARGS(ntohl(dest_ip)),
                 (int)id, (int)seq);

    /* Send via IP layer */
    int ret = ip_send(dev, dest_ip, IPPROTO_ICMP, icmp_packet, icmp_len);

    kfree(icmp_packet);
    return ret;
}

/**
 * icmp_init - Initialize ICMP protocol
 * @return: 0 on success, -1 on error
 */
int icmp_init(void)
{
    LOG_INFO("ICMP", "Initializing ICMP protocol");

    /* Register ICMP protocol handler */
    memset(&icmp_proto, 0, sizeof(icmp_proto));
    icmp_proto.name = "ICMP";
    icmp_proto.ethertype = 0;  /* ICMP doesn't have an EtherType */
    icmp_proto.ipproto = IPPROTO_ICMP;
    icmp_proto.handler = icmp_receive_handler;
    icmp_proto.priv = NULL;

    if (net_register_protocol(&icmp_proto) != 0)
    {
        LOG_ERROR("ICMP", "Failed to register ICMP protocol");
        return -1;
    }

    LOG_INFO("ICMP", "ICMP protocol initialized");
    return 0;
}

