/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - TCP Protocol (OSDev-inspired minimal port)
 * Copyright (C) 2026 Iván Rodriguez
 *
 * TCP protocol layer: receive handler, checksum, LISTEN, handshake.
 * No socket API yet - connections reach ESTABLISHED for future accept/recv.
 */

#include "tcp.h"
#include "ip.h"
#include "arp.h"
#include <ir0/kmem.h>
#include <ir0/logging.h>
#include <drivers/timer/clock_system.h>
#include <string.h>

static struct net_protocol tcp_proto;

/* Listening ports: simple linked list */
struct tcp_listener {
    uint16_t port;
    struct tcp_listener *next;
};
static struct tcp_listener *tcp_listeners = NULL;

/* Active connections (SYN_RECEIVED, ESTABLISHED, etc.) */
struct tcp_conn {
    ip4_addr_t saddr;   /* our local IP */
    uint16_t sport;     /* our port */
    ip4_addr_t daddr;   /* peer IP */
    uint16_t dport;     /* peer port */
    int state;
    uint32_t iss;       /* our ISN */
    uint32_t irs;       /* peer's ISN */
    uint32_t snd_nxt;
    uint32_t snd_una;
    uint32_t rcv_nxt;
    struct tcp_conn *next;
};
#define MAX_TCP_CONNS 32
static struct tcp_conn *tcp_conns = NULL;
static int tcp_conn_count = 0;

static uint32_t tcp_isn_counter = 1;

static uint32_t tcp_new_isn(void)
{
    uint64_t ms = clock_get_uptime_milliseconds();
    return (uint32_t)((ms * 64000) + (tcp_isn_counter++ * 12345)) & 0x7FFFFFFF;
}

/**
 * tcp_checksum - Calculate TCP checksum (pseudo-header + TCP segment)
 * RFC 793: TCP checksum = ones' complement of ones' complement sum
 */
uint16_t tcp_checksum(ip4_addr_t src_ip, ip4_addr_t dest_ip,
                      const struct tcphdr *tcph, size_t len)
{
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
    pseudo_header.protocol = IPPROTO_TCP;
    pseudo_header.length = htons((uint16_t)len);

    uint32_t sum = 0;
    uint16_t buf[16];
    memcpy(buf, &pseudo_header, sizeof(pseudo_header));
    for (size_t i = 0; i < sizeof(pseudo_header) / 2; i++)
        sum += ntohs(buf[i]);

    const uint8_t *p = (const uint8_t *)tcph;
    for (size_t i = 0; i < len / 2; i++)
        sum += ((uint16_t)p[i * 2] << 8) | p[i * 2 + 1];

    if (len & 1)
        sum += ((const uint8_t *)tcph)[len - 1] << 8;

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return htons(~sum);
}

/**
 * tcp_send_segment - Send a TCP segment
 * Used for RST, SYN-ACK, ACK, etc.
 */
static int tcp_send_segment(struct net_device *dev, ip4_addr_t dest_ip,
                            uint16_t src_port, uint16_t dest_port,
                            uint32_t seq, uint32_t ack_seq, uint16_t flags,
                            const void *payload, size_t payload_len)
{
    size_t hdr_len = sizeof(struct tcphdr);
    size_t total_len = hdr_len + payload_len;

    uint8_t *segment = kmalloc(total_len);
    if (!segment)
        return -1;

    struct tcphdr *th = (struct tcphdr *)segment;
    th->source = htons(src_port);
    th->dest = htons(dest_port);
    th->seq = htonl(seq);
    th->ack_seq = htonl(ack_seq);
    th->flags = htons(TCP_DOFF_SET(5) | (flags & 0x1FF));
    th->window = htons(TCP_DEFAULT_WINDOW);
    th->check = 0;
    th->urg_ptr = 0;

    if (payload_len)
        memcpy(segment + hdr_len, payload, payload_len);

    ip4_addr_t src_ip = ip_local_addr;
    if (arp_get_interface_ip(dev, &src_ip) != 0)
        src_ip = ip_local_addr;

    th->check = tcp_checksum(src_ip, dest_ip, th, total_len);

    int ret = ip_send(dev, dest_ip, IPPROTO_TCP, segment, total_len);
    kfree(segment);
    return ret;
}

/**
 * tcp_send_rst - Send RST in response to invalid/unwanted segment
 * @seq: our seq (0 if no connection), ack_seq: their seq + 1 (or + data_len for data)
 */
static void tcp_send_rst(struct net_device *dev, ip4_addr_t src_ip, ip4_addr_t dest_ip,
                         uint16_t src_port, uint16_t dest_port,
                         uint32_t seq, uint32_t rst_ack)
{
    (void)dest_ip;
    tcp_send_segment(dev, src_ip, dest_port, src_port, seq, rst_ack, TCP_FLAG_RST, NULL, 0);
}

static struct tcp_listener *tcp_find_listener(uint16_t port)
{
    struct tcp_listener *l = tcp_listeners;
    while (l)
    {
        if (l->port == port)
            return l;
        l = l->next;
    }
    return NULL;
}

static struct tcp_conn *tcp_find_conn(ip4_addr_t saddr, uint16_t sport,
                                      ip4_addr_t daddr, uint16_t dport)
{
    struct tcp_conn *c = tcp_conns;
    while (c)
    {
        if (c->saddr == saddr && c->sport == sport && c->daddr == daddr && c->dport == dport)
            return c;
        c = c->next;
    }
    return NULL;
}

static void tcp_conn_remove(struct tcp_conn *conn)
{
    struct tcp_conn **p = &tcp_conns;
    while (*p)
    {
        if (*p == conn)
        {
            *p = conn->next;
            tcp_conn_count--;
            kfree(conn);
            return;
        }
        p = &(*p)->next;
    }
}

/**
 * tcp_receive_handler - Process incoming TCP packets
 */
void tcp_receive_handler(struct net_device *dev, const void *data, size_t len, void *priv)
{
    (void)priv;

    if (len < sizeof(struct tcphdr))
    {
        LOG_WARNING("TCP", "Packet too short");
        return;
    }

    const struct tcphdr *th = (const struct tcphdr *)data;
    uint16_t doff = TCP_DOFF_GET(ntohs(th->flags));
    size_t hdr_len = doff * 4;

    if (hdr_len < 20 || hdr_len > len)
    {
        LOG_WARNING_FMT("TCP", "Invalid header length: doff=%d", doff);
        return;
    }

    ip4_addr_t src_ip = ip_get_last_src_addr();
    ip4_addr_t dest_ip = ip_local_addr;
    if (arp_get_interface_ip(dev, &dest_ip) != 0)
        dest_ip = ip_local_addr;

    /* Verify checksum */
    uint16_t recv_check = th->check;
    struct tcphdr th_copy;
    memcpy(&th_copy, th, sizeof(struct tcphdr));
    th_copy.check = 0;
    uint16_t calc = tcp_checksum(src_ip, dest_ip, &th_copy, len);
    if (calc != recv_check)
    {
        LOG_WARNING_FMT("TCP", "Checksum mismatch: recv=0x%04x calc=0x%04x",
                        ntohs(recv_check), ntohs(calc));
        return;
    }

    uint16_t src_port = ntohs(th->source);
    uint16_t dest_port = ntohs(th->dest);
    uint32_t seq = ntohl(th->seq);
    uint32_t ack_seq = ntohl(th->ack_seq);
    uint16_t flags = TCP_FLAGS_GET(ntohs(th->flags));
    size_t payload_len = len - hdr_len;

    /* RST: ack = their seq + (1 for SYN/FIN, or payload_len for data) */
    uint32_t rst_ack = seq + (payload_len ? payload_len : 1);
    if (flags & TCP_FLAG_SYN)
        rst_ack = seq + 1;

    LOG_INFO_FMT("TCP", "Recv: " IP4_FMT ":%d -> " IP4_FMT ":%d seq=%u ack=%u flags=0x%x",
                IP4_ARGS(ntohl(src_ip)), src_port,
                IP4_ARGS(ntohl(dest_ip)), dest_port,
                seq, ack_seq, flags);

    /* Look up existing connection: our (saddr,sport) = (dest_ip,dest_port), peer = (src_ip,src_port) */
    struct tcp_conn *conn = tcp_find_conn(dest_ip, dest_port, src_ip, src_port);

    if (conn)
    {
        if (flags & TCP_FLAG_RST)
        {
            LOG_INFO("TCP", "RST received, closing connection");
            tcp_conn_remove(conn);
            return;
        }

        if (conn->state == TCP_SYN_RECEIVED)
        {
            if ((flags & TCP_FLAG_ACK) && ack_seq == conn->snd_nxt)
            {
                conn->snd_una = ack_seq;
                conn->state = TCP_ESTABLISHED;
                LOG_INFO_FMT("TCP", "Connection ESTABLISHED " IP4_FMT ":%d",
                            IP4_ARGS(ntohl(src_ip)), src_port);
                return;
            }
            if (!(flags & TCP_FLAG_ACK))
            {
                LOG_WARNING("TCP", "SYN_RECEIVED: expected ACK, dropping");
                return;
            }
        }
        else if (conn->state == TCP_ESTABLISHED)
        {
            if (flags & TCP_FLAG_ACK)
            {
                /* Update snd_una if they ack our data */
                if (tcp_seq_gt(ack_seq, conn->snd_una) && tcp_seq_leq(ack_seq, conn->snd_nxt))
                    conn->snd_una = ack_seq;
            }
            if (flags & TCP_FLAG_FIN)
            {
                conn->rcv_nxt = seq + 1;
                tcp_send_segment(dev, src_ip, dest_port, src_port,
                                conn->snd_nxt, conn->rcv_nxt, TCP_FLAG_ACK, NULL, 0);
                conn->state = TCP_CLOSE_WAIT;
                LOG_INFO("TCP", "FIN received, sent ACK, CLOSE_WAIT");
            }
            if (payload_len > 0)
            {
                conn->rcv_nxt = seq + payload_len;
                tcp_send_segment(dev, src_ip, dest_port, src_port,
                                conn->snd_nxt, conn->rcv_nxt, TCP_FLAG_ACK, NULL, 0);
            }
            return;
        }
        else if (conn->state == TCP_CLOSE_WAIT)
        {
            if (flags & TCP_FLAG_ACK)
                conn->snd_una = ack_seq;
            return;
        }
    }

    /* No connection - check for SYN to listening port */
    if ((flags & TCP_FLAG_SYN) && !(flags & TCP_FLAG_ACK))
    {
        struct tcp_listener *listener = tcp_find_listener(dest_port);
        if (listener && tcp_conn_count < MAX_TCP_CONNS)
        {
            struct tcp_conn *new_conn = kmalloc(sizeof(struct tcp_conn));
            if (new_conn)
            {
                new_conn->saddr = dest_ip;
                new_conn->sport = dest_port;
                new_conn->daddr = src_ip;
                new_conn->dport = src_port;
                new_conn->irs = seq;
                new_conn->rcv_nxt = seq + 1;
                new_conn->iss = tcp_new_isn();
                new_conn->snd_nxt = new_conn->iss + 1;
                new_conn->snd_una = new_conn->iss;
                new_conn->state = TCP_SYN_RECEIVED;

                new_conn->next = tcp_conns;
                tcp_conns = new_conn;
                tcp_conn_count++;

                tcp_send_segment(dev, src_ip, dest_port, src_port,
                                new_conn->iss, new_conn->rcv_nxt,
                                TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
                LOG_INFO_FMT("TCP", "SYN received, sent SYN-ACK (conn in SYN_RECEIVED)");
                return;
            }
        }
    }

    /* No handler - send RST */
    tcp_send_rst(dev, src_ip, dest_ip, src_port, dest_port, 0, rst_ack);
}

int tcp_listen(uint16_t port)
{
    if (tcp_find_listener(port))
    {
        LOG_WARNING_FMT("TCP", "Port %d already listening", (int)port);
        return -1;
    }
    struct tcp_listener *l = kmalloc(sizeof(struct tcp_listener));
    if (!l)
        return -1;
    l->port = port;
    l->next = tcp_listeners;
    tcp_listeners = l;
    LOG_INFO_FMT("TCP", "Listening on port %d", (int)port);
    return 0;
}

int tcp_init(void)
{
    LOG_INFO("TCP", "Initializing TCP protocol");

    memset(&tcp_proto, 0, sizeof(tcp_proto));
    tcp_proto.name = "TCP";
    tcp_proto.ethertype = 0;
    tcp_proto.ipproto = IPPROTO_TCP;
    tcp_proto.handler = tcp_receive_handler;
    tcp_proto.priv = NULL;

    if (net_register_protocol(&tcp_proto) != 0)
    {
        LOG_ERROR("TCP", "Failed to register TCP protocol");
        return -1;
    }

    /* Default: listen on port 80 for HTTP/testing */
    tcp_listen(80);

    LOG_INFO("TCP", "TCP protocol initialized");
    return 0;
}
