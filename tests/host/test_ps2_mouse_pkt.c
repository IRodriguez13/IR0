/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_ps2_mouse_pkt.c
 * Description: Host tests for PS/2 mouse packet assembler + kbd isolation.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <ir0/ps2_mouse_pkt.h>
#include <ir0/ps2_set1.h>

void test_ps2_mouse_pkt_resync_and_complete(void)
{
	struct ps2_mouse_pkt_state st;
	struct ps2_mouse_pkt pkt;

	TEST_BEGIN("ps2_mouse_pkt resync + 3-byte complete");
	ps2_mouse_pkt_reset(&st, 3);

	/* Garbage without bit3 — dropped, no complete packet. */
	ps2_mouse_pkt_feed(&st, 0x01, &pkt);
	ASSERT(!pkt.complete);
	ps2_mouse_pkt_feed(&st, 0x02, &pkt);
	ASSERT(!pkt.complete);

	/* Valid 3-byte packet: flags=0x08, dx=1, dy=2 */
	ps2_mouse_pkt_feed(&st, 0x08, &pkt);
	ASSERT(!pkt.complete);
	ps2_mouse_pkt_feed(&st, 0x01, &pkt);
	ASSERT(!pkt.complete);
	ps2_mouse_pkt_feed(&st, 0x02, &pkt);
	ASSERT(pkt.complete);
	ASSERT_EQ(pkt.flags, 0x08);
	ASSERT_EQ(pkt.dx, 1);
	ASSERT_EQ(pkt.dy, 2);

	/* Incomplete trailing byte alone does not publish. */
	ps2_mouse_pkt_feed(&st, 0x08, &pkt);
	ASSERT(!pkt.complete);
	TEST_END();
}

void test_ps2_mouse_interleaved_does_not_touch_kbd_mods(void)
{
	struct ps2_set1_state kbd;
	struct ps2_set1_result kr;
	struct ps2_mouse_pkt_state mouse;
	struct ps2_mouse_pkt mpkt;

	TEST_BEGIN("ps2 mouse packets never alter keyboard modifiers");
	ps2_set1_reset(&kbd);
	ps2_mouse_pkt_reset(&mouse, 3);

	/* Establish Left Ctrl via keyboard path only. */
	ps2_set1_feed(&kbd, 0x1D, &kr);
	ASSERT(ps2_set1_ctrl(&kbd.mods));

	/*
	 * Mouse motion bytes (would look like scancodes if misrouted).
	 * They must be consumed by the mouse assembler, not ps2_set1.
	 */
	ps2_mouse_pkt_feed(&mouse, 0x08, &mpkt);
	ps2_mouse_pkt_feed(&mouse, 0x1D, &mpkt); /* same numeric as Ctrl make */
	ps2_mouse_pkt_feed(&mouse, 0x9D, &mpkt); /* same numeric as Ctrl break */
	ASSERT(mpkt.complete);

	/* Keyboard modifiers unchanged — mouse path did not call set1. */
	ASSERT(kbd.mods.left_ctrl);
	ASSERT(ps2_set1_ctrl(&kbd.mods));

	/* Real Ctrl break still clears via keyboard path. */
	ps2_set1_feed(&kbd, 0x9D, &kr);
	ASSERT(!ps2_set1_ctrl(&kbd.mods));
	ASSERT_EQ(ps2_set1_mods_mask(&kbd.mods), KBD_MOD_NONE);
	TEST_END();
}

void test_ps2_set1_alt_independent(void)
{
	struct ps2_set1_state st;
	struct ps2_set1_result r;

	TEST_BEGIN("ps2_set1 Left/Right Alt independent");
	ps2_set1_reset(&st);

	ps2_set1_feed(&st, 0x38, &r); /* LAlt make */
	ASSERT(st.mods.left_alt);
	ps2_set1_feed(&st, 0xE0, &r);
	ps2_set1_feed(&st, 0x38, &r); /* RAlt make */
	ASSERT(st.mods.left_alt && st.mods.right_alt);

	ps2_set1_feed(&st, 0xB8, &r); /* LAlt break */
	ASSERT(!st.mods.left_alt);
	ASSERT(st.mods.right_alt);

	ps2_set1_feed(&st, 0xE0, &r);
	ps2_set1_feed(&st, 0xB8, &r); /* RAlt break */
	ASSERT(!st.mods.right_alt);
	ASSERT_EQ(ps2_set1_mods_mask(&st.mods), KBD_MOD_NONE);
	TEST_END();
}
