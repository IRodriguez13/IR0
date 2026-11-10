/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: tcp_internal.h
 * Description: Internal facade shared by the net/tcp*.c units (socket API,
 *              output, congestion control, passive open, input demux).
 *              Not a public API: other subsystems use net/tcp.h.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <ir0/arch_port.h>
#include "tcp.h"

#define TCP_WIRE_TIMEOUT_MS 5000U
#define TCP_WIRE_POLL_MS    5U
#define TCP_WIRE_RTO_MS     80U
#define TCP_WIRE_REXMIT_MAX 3U
#define TCP_WIRE_LISTEN_MAX 4
#define TCP_WIRE_CONN_MAX   4
#define TCP_WIRE_RX_MAX     4096
#define TCP_WIRE_TX_MAX     2048
#define TCP_WIRE_PEER_CC_PORT 8890U /* guest→host peer-cc smoke (loss probe) */
#define TCP_WIRE_MSS        536U
#define TCP_WIRE_CWND_CAP   8U
#define TCP_WIRE_SB_MAX     4U

struct tcp_out_seg
{
	uint32_t seq;
	uint16_t len;
	uint8_t data[TCP_WIRE_MSS];
	uint8_t active;
	uint8_t sent;
	uint8_t acked;
	uint8_t sack_covered;
};

struct tcp_pending_conn
{
	int active;
	ip4_addr_t peer_ip;
	uint16_t peer_port;
	uint16_t local_port;
	uint32_t local_seq;
	uint32_t peer_ack;
	uint16_t peer_window;
	int synack_seen;
	uint64_t deadline_ms;
};

struct tcp_wire_listener
{
	int active;
	uint16_t port;
};

struct tcp_wire_inbound
{
	int active;
	int established;
	int taken;
	int peer_fin;
	uint16_t local_port;
	ip4_addr_t peer_ip;
	uint16_t peer_port;
	uint32_t local_seq;
	uint32_t peer_ack;
	uint16_t peer_window;
	uint8_t rx[TCP_WIRE_RX_MAX];
	unsigned rx_len;
	unsigned rx_off;
};

/* Single outbound client association (F8 smoke path). */
struct tcp_wire_outbound
{
	int active;
	ip4_addr_t peer_ip;
	uint16_t peer_port;
	uint16_t local_port;
	uint32_t snd_nxt;
	uint32_t snd_una;	 /* highest peer ACK of our sequence space */
	uint32_t ack_to_peer;	 /* ACK field we put on outbound segments */
	uint16_t peer_window;
	struct tcp_out_seg sb[TCP_WIRE_SB_MAX];
	unsigned sb_count;
	uint32_t unpaid_seq;	 /* lowest outstanding seq (scoreboard cache) */
	unsigned unpaid_len;	 /* total outstanding bytes (scoreboard cache) */
	unsigned rexmit_total;
	int in_recovery;
	uint32_t recover_seq;
	int recovery_selftest_done;
	int recovery_hole_rexmit;
	int reno_recovery_ok_logged;
	int reno_recovery_print_pending;
	unsigned cwnd;		 /* slow-start window in MSS units */
	unsigned ssthresh;	 /* Reno slow-start threshold (MSS units) */
	unsigned dup_ack_count;
	int drop_next_tx;
	int peer_cc_mode;	 /* port 8890: loss probe + no synthetic CC */
	int window_ok_logged;
	int rexmit_ok_logged;
	int cwnd_ok_logged;
	int cwnd_print_pending;
	int fast_rexmit_pending;
	int dupack_ok_logged;
	int dupack_print_pending;
	int sack_ok_logged;
	int sack_print_pending;
	int reno_applied;
	uint8_t rx[TCP_WIRE_RX_MAX];
	unsigned rx_len;
	unsigned rx_off;
	int peer_fin;
};

/* Connection state (tcp.c: outbound + pending connect; tcp_listen.c: passive). */
extern struct tcp_pending_conn tcp_wire_pending;
extern struct tcp_wire_outbound tcp_wire_out;
extern struct tcp_wire_listener tcp_wire_listeners[TCP_WIRE_LISTEN_MAX];
extern struct tcp_wire_inbound tcp_wire_inbound_conns[TCP_WIRE_CONN_MAX];

static inline uint64_t tcp_irq_save(void)
{
	return (uint64_t)irq_save();
}

static inline void tcp_irq_restore(uint64_t flags)
{
	irq_restore((unsigned long)flags);
}
/* Segment output (tcp_output.c). */
int tcp_send_raw(struct net_device *dev, ip4_addr_t dest_ip,
		 const struct tcp_header *hdr, const void *payload,
		 size_t payload_len);
uint16_t tcp_pick_ephemeral(void);
int tcp_build_header(struct tcp_header *hdr, uint16_t src_port,
		     uint16_t dest_port, uint32_t seq, uint32_t ack,
		     uint8_t flags);

/* Sender scoreboard and congestion control (tcp_cc.c). */
void tcp_sb_clear(void);
int tcp_sb_add(const void *data, unsigned len, uint32_t seq);
struct tcp_out_seg *tcp_sb_first_outstanding(void);
int tcp_sb_xmit_seg(struct net_device *dev, ip4_addr_t peer_ip,
		    uint16_t peer_port, uint16_t local_port,
		    struct tcp_out_seg *s, uint32_t ack_peer);
int tcp_sb_rexmit_hole(struct net_device *dev, ip4_addr_t peer_ip,
		       uint16_t peer_port, uint16_t local_port,
		       uint32_t ack_peer);
void tcp_out_note_sack_block(uint32_t left, uint32_t right);
void tcp_parse_sack_options(const uint8_t *opts, size_t optlen);
void tcp_out_note_ack(uint32_t ack, uint16_t window);

/* Passive open (tcp_listen.c). */
struct tcp_wire_inbound *tcp_inbound_find(ip4_addr_t peer_ip,
					  uint16_t peer_port,
					  uint16_t local_port);
void tcp_inbound_free(struct tcp_wire_inbound *c);
void tcp_handle_listen_syn(struct net_device *dev, ip4_addr_t src_ip,
			   uint16_t src_port, uint16_t dest_port,
			   uint32_t seq);
void tcp_handle_inbound(struct net_device *dev, ip4_addr_t src_ip,
			uint16_t src_port, uint16_t dest_port,
			uint32_t seq, uint32_t ack, uint8_t flags,
			const uint8_t *payload, size_t payload_len);
