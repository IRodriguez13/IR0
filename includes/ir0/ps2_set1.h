/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ps2_set1.h
 * Description: PS/2 scancode set 1 decoder + L/R modifier state (portable).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <stddef.h>

/*
 * IR0 uses IBM PC AT/2 scan code set 1 (XT make/break):
 *   make  = code
 *   break = code | 0x80
 *   extended = 0xE0 prefix, then make/break
 * Ref: https://wiki.osdev.org/PS/2_Keyboard (Scan Code Set 1)
 */

struct keyboard_modifiers
{
	uint8_t left_ctrl;
	uint8_t right_ctrl;
	uint8_t left_shift;
	uint8_t right_shift;
	uint8_t left_alt;
	uint8_t right_alt;
	uint8_t caps_lock;
	uint8_t num_lock;
	uint8_t scroll_lock;
};

/* Bitmask for ASSERT(modifiers == NONE) style checks. */
#define KBD_MOD_LEFT_CTRL    (1u << 0)
#define KBD_MOD_RIGHT_CTRL   (1u << 1)
#define KBD_MOD_LEFT_SHIFT   (1u << 2)
#define KBD_MOD_RIGHT_SHIFT  (1u << 3)
#define KBD_MOD_LEFT_ALT     (1u << 4)
#define KBD_MOD_RIGHT_ALT    (1u << 5)
#define KBD_MOD_CAPS_LOCK    (1u << 6)
#define KBD_MOD_NUM_LOCK     (1u << 7)
#define KBD_MOD_SCROLL_LOCK  (1u << 8)
#define KBD_MOD_NONE         0u

struct ps2_set1_state
{
	struct keyboard_modifiers mods;
	uint8_t e0_prefix; /* next byte is extended */
	uint8_t e1_skip;   /* bytes remaining to discard (Pause) */
	int layout;        /* 0=US 1=LATAM — same as KEYBOARD_LAYOUT_* */
};

enum ps2_set1_key_name
{
	PS2_KEY_NONE = 0,
	PS2_KEY_LEFT_CTRL,
	PS2_KEY_RIGHT_CTRL,
	PS2_KEY_LEFT_SHIFT,
	PS2_KEY_RIGHT_SHIFT,
	PS2_KEY_LEFT_ALT,
	PS2_KEY_RIGHT_ALT,
	PS2_KEY_CAPS_LOCK,
	PS2_KEY_NUM_LOCK,
	PS2_KEY_SCROLL_LOCK,
	PS2_KEY_OTHER,
};

struct ps2_set1_result
{
	uint8_t raw;
	uint8_t prefix; /* 0, 0xE0, or 0xE1 */
	uint8_t down;   /* 1=make 0=break */
	uint8_t key;    /* enum ps2_set1_key_name */
	uint32_t mods_before;
	uint32_t mods_after;
	/* Bytes delivered to the TTY ring (ASCII or CSI). */
	uint8_t emitted[8];
	uint8_t emitted_len;
};

void ps2_set1_reset(struct ps2_set1_state *st);
void ps2_set1_all_keys_up(struct ps2_set1_state *st);

int ps2_set1_ctrl(const struct keyboard_modifiers *m);
int ps2_set1_shift(const struct keyboard_modifiers *m);
int ps2_set1_alt(const struct keyboard_modifiers *m);
uint32_t ps2_set1_mods_mask(const struct keyboard_modifiers *m);
const char *ps2_set1_key_name(uint8_t key);

/*
 * Feed one controller byte. Fills @out; always safe with out!=NULL.
 * Does not touch TTY/console — caller decides where to push emitted bytes.
 */
void ps2_set1_feed(struct ps2_set1_state *st, uint8_t raw,
		   struct ps2_set1_result *out);
