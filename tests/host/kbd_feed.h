/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kbd_feed.h
 * Description: Host test helper: scancode-set-1 bytes through the PS/2
 *              decoder and then the console keyboard translator, the path
 *              a key takes from IRQ1 to the console.
 */

#pragma once

#include <ir0/kbd.h>
#include <ir0/ps2_set1.h>
#include <linux/input-event-codes.h>
#include <string.h>

struct kbd_feed
{
	struct ps2_set1_state dec;
	struct keyboard_modifiers mods;
};

struct kbd_result
{
	unsigned int keycode;
	int down;
	uint8_t emitted[8];
	size_t emitted_len;
};

static inline void kbd_feed_reset(struct kbd_feed *k)
{
	ps2_set1_reset(&k->dec);
	memset(&k->mods, 0, sizeof(k->mods));
}

static inline void kbd_feed(struct kbd_feed *k, uint8_t raw,
			    struct kbd_result *r)
{
	unsigned int kc;
	int down;

	r->emitted_len = 0;
	r->keycode = 0;
	r->down = 0;
	if (!ps2_set1_decode(&k->dec, raw, &kc, &down) || !kc)
		return;
	r->keycode = kc;
	r->down = down;
	r->emitted_len = kbd_translate(&k->mods, kc, down, r->emitted);
}
