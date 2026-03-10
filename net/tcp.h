/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - TCP Protocol (OSDev-inspired)
 * Copyright (C) 2026 Iván Rodriguez
 *
 * TCP header and constants. Minimal implementation for protocol layer.
 */

#pragma once

#include <ir0/net.h>
#include <stdint.h>
#include <stdbool.h>

/* TCP header (RFC 793) */
struct tcphdr
{
    uint16_t source;
    uint16_t dest;
    uint32_t seq;
    uint32_t ack_seq;
    uint16_t flags;
    uint16_t window;
    uint16_t check;
    uint16_t urg_ptr;
} __attribute__((packed));

/* TCP flags */
#define TCP_FLAG_FIN  0x0001
#define TCP_FLAG_SYN  0x0002
#define TCP_FLAG_RST  0x0004
#define TCP_FLAG_PSH  0x0008
#define TCP_FLAG_ACK  0x0010
#define TCP_FLAG_URG  0x0020

#define TCP_DOFF_GET(flags) (((flags) >> 12) & 0xF)
#define TCP_DOFF_SET(words) (((words) & 0xF) << 12)
#define TCP_FLAGS_GET(flags) ((flags) & 0x1FF)

/* TCP states */
#define TCP_CLOSED        0
#define TCP_LISTEN        1
#define TCP_SYN_SENT      2
#define TCP_SYN_RECEIVED  3
#define TCP_ESTABLISHED   4
#define TCP_FIN_WAIT_1    5
#define TCP_FIN_WAIT_2    6
#define TCP_CLOSE_WAIT    7
#define TCP_CLOSING       8
#define TCP_LAST_ACK      9
#define TCP_TIME_WAIT     10

/* TCP constants */
#define TCP_MSS            1460
#define TCP_DEFAULT_WINDOW 8192
#define TCP_EPHEMERAL_MIN  32768
#define TCP_EPHEMERAL_MAX  65535

/* Sequence number helpers */
static inline bool tcp_seq_lt(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) < 0;
}
static inline bool tcp_seq_leq(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) <= 0;
}
static inline bool tcp_seq_gt(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}
static inline bool tcp_seq_geq(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) >= 0;
}

/* TCP checksum (pseudo-header + TCP segment) */
uint16_t tcp_checksum(ip4_addr_t src_ip, ip4_addr_t dest_ip,
                      const struct tcphdr *tcph, size_t len);

/* TCP init - register with IP layer */
int tcp_init(void);

/* TCP listen - register port for incoming connections */
int tcp_listen(uint16_t port);
