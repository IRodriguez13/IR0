/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ps2_mouse_pkt.h
 * Description: Portable PS/2 mouse packet assembler (resync + 3/4-byte).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/* Packet header bit 3 must be set (OSDev / IBM PS/2 mouse). */
#define PS2_MOUSE_PKT_ALWAYS_1 0x08u

struct ps2_mouse_pkt_state
{
	uint8_t buf[4];
	uint8_t index;
	uint8_t expected; /* 3 or 4 */
};

struct ps2_mouse_pkt
{
	uint8_t flags;
	int16_t dx;
	int16_t dy;
	int8_t wheel;
	uint8_t extra_buttons;
	uint8_t complete; /* 1 when a full packet was produced */
};

void ps2_mouse_pkt_reset(struct ps2_mouse_pkt_state *st, uint8_t expected_bytes);
void ps2_mouse_pkt_feed(struct ps2_mouse_pkt_state *st, uint8_t data,
			struct ps2_mouse_pkt *out);
