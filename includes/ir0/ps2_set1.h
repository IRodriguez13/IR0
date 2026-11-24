/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ps2_set1.h
 * Description: PS/2 scancode set 1 decoder (what an i8042 in translate mode
 *              delivers): E0/E1 prefixes, make/break, Linux keycodes.
 */

#pragma once

#include <stdint.h>

struct ps2_set1_state
{
	uint8_t e0_prefix; /* next byte is extended */
	uint8_t e1_skip;   /* bytes remaining to discard (Pause) */
};

void ps2_set1_reset(struct ps2_set1_state *st);

/*
 * Feed one byte. 1 when it completes a key: *keycode is the Linux KEY_*
 * (0 for a key without one) and *down 1 for make, 0 for break; 0 while a
 * prefix or a Pause sequence is still pending.
 */
int ps2_set1_decode(struct ps2_set1_state *st, uint8_t raw,
		    unsigned int *keycode, int *down);

/* Set the bit of every keycode the decoder can produce. */
void ps2_set1_keybits(unsigned long *keybit);
