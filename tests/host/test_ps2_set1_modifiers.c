/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_ps2_set1_modifiers.c
 * Description: Host unit tests for PS/2 set-1 modifier make/break model.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <ir0/ps2_set1.h>
#include <string.h>

static void feed(struct ps2_set1_state *st, uint8_t raw, struct ps2_set1_result *r)
{
	ps2_set1_feed(st, raw, r);
}

static int emitted_eq(const struct ps2_set1_result *r, uint8_t b)
{
	return r->emitted_len == 1 && r->emitted[0] == b;
}

void test_ps2_set1_ctrl_x_then_a(void)
{
	struct ps2_set1_state st;
	struct ps2_set1_result r;

	TEST_BEGIN("ps2_set1 Ctrl-X then A clears Ctrl");
	ps2_set1_reset(&st);

	feed(&st, 0x1D, &r); /* Left Ctrl down */
	ASSERT(r.down == 1);
	ASSERT(r.key == PS2_KEY_LEFT_CTRL);
	ASSERT(ps2_set1_ctrl(&st.mods));

	feed(&st, 0x2D, &r); /* X down */
	ASSERT(emitted_eq(&r, 0x18));

	feed(&st, 0xAD, &r); /* X up */
	ASSERT(r.down == 0);
	ASSERT(r.emitted_len == 0);
	ASSERT(ps2_set1_ctrl(&st.mods));

	feed(&st, 0x9D, &r); /* Left Ctrl up */
	ASSERT(r.down == 0);
	ASSERT(r.key == PS2_KEY_LEFT_CTRL);
	ASSERT(!ps2_set1_ctrl(&st.mods));
	ASSERT_EQ(ps2_set1_mods_mask(&st.mods), KBD_MOD_NONE);

	feed(&st, 0x1E, &r); /* A down */
	ASSERT(emitted_eq(&r, (uint8_t)'a'));
	ASSERT(!ps2_set1_ctrl(&st.mods));

	feed(&st, 0x9E, &r); /* A up */
	ASSERT_EQ(ps2_set1_mods_mask(&st.mods), KBD_MOD_NONE);
	TEST_END();
}

void test_ps2_set1_left_right_ctrl_independent(void)
{
	struct ps2_set1_state st;
	struct ps2_set1_result r;

	TEST_BEGIN("ps2_set1 Left/Right Ctrl independent");
	ps2_set1_reset(&st);

	feed(&st, 0x1D, &r);
	ASSERT(st.mods.left_ctrl);
	ASSERT(!st.mods.right_ctrl);

	feed(&st, 0xE0, &r);
	feed(&st, 0x1D, &r); /* Right Ctrl down */
	ASSERT(st.mods.left_ctrl);
	ASSERT(st.mods.right_ctrl);
	ASSERT(ps2_set1_ctrl(&st.mods));

	feed(&st, 0x9D, &r); /* Left Ctrl up — Right still down */
	ASSERT(!st.mods.left_ctrl);
	ASSERT(st.mods.right_ctrl);
	ASSERT(ps2_set1_ctrl(&st.mods));

	feed(&st, 0xE0, &r);
	feed(&st, 0x9D, &r); /* Right Ctrl up */
	ASSERT(!ps2_set1_ctrl(&st.mods));
	ASSERT_EQ(ps2_set1_mods_mask(&st.mods), KBD_MOD_NONE);
	TEST_END();
}

void test_ps2_set1_shift_a(void)
{
	struct ps2_set1_state st;
	struct ps2_set1_result r;

	TEST_BEGIN("ps2_set1 Shift+A then plain A");
	ps2_set1_reset(&st);

	feed(&st, 0x2A, &r); /* LShift down */
	feed(&st, 0x1E, &r);
	ASSERT(emitted_eq(&r, (uint8_t)'A'));
	feed(&st, 0x9E, &r);
	feed(&st, 0xAA, &r); /* LShift up */
	ASSERT_EQ(ps2_set1_mods_mask(&st.mods), KBD_MOD_NONE);

	feed(&st, 0x1E, &r);
	ASSERT(emitted_eq(&r, (uint8_t)'a'));
	TEST_END();
}

void test_ps2_set1_caps_toggle(void)
{
	struct ps2_set1_state st;
	struct ps2_set1_result r;

	TEST_BEGIN("ps2_set1 Caps Lock toggles on press only");
	ps2_set1_reset(&st);

	feed(&st, 0x3A, &r); /* Caps make */
	ASSERT(st.mods.caps_lock);
	feed(&st, 0xBA, &r); /* Caps break — still on */
	ASSERT(st.mods.caps_lock);

	feed(&st, 0x1E, &r);
	ASSERT(emitted_eq(&r, (uint8_t)'A'));

	feed(&st, 0x3A, &r); /* Caps make again → off */
	ASSERT(!st.mods.caps_lock);
	TEST_END();
}

void test_ps2_set1_arrows_e0(void)
{
	struct ps2_set1_state st;
	struct ps2_set1_result r;

	TEST_BEGIN("ps2_set1 extended arrow CSI");
	ps2_set1_reset(&st);

	feed(&st, 0xE0, &r);
	feed(&st, 0x48, &r); /* Up */
	ASSERT(r.emitted_len == 3);
	ASSERT(r.emitted[0] == 0x1b && r.emitted[1] == '[' && r.emitted[2] == 'A');

	feed(&st, 0xE0, &r);
	feed(&st, 0xC8, &r); /* Up break — no emit */
	ASSERT(r.emitted_len == 0);
	ASSERT_EQ(ps2_set1_mods_mask(&st.mods), KBD_MOD_NONE);
	TEST_END();
}

void test_ps2_set1_autorepeat_ctrl(void)
{
	struct ps2_set1_state st;
	struct ps2_set1_result r;

	TEST_BEGIN("ps2_set1 Ctrl autorepeat does not invent key-up");
	ps2_set1_reset(&st);

	feed(&st, 0x1D, &r);
	feed(&st, 0x1D, &r); /* repeat make */
	ASSERT(st.mods.left_ctrl);
	ASSERT(ps2_set1_ctrl(&st.mods));
	feed(&st, 0x9D, &r);
	ASSERT(!ps2_set1_ctrl(&st.mods));
	ASSERT_EQ(ps2_set1_mods_mask(&st.mods), KBD_MOD_NONE);
	TEST_END();
}
