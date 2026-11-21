/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: keymap.h
 * Description: Console keyboard translation tables (Linux keysyms per keycode).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/*
 * Four tables (K_NORMTAB, K_SHIFTTAB, K_ALTTAB, K_ALTSHIFTTAB) of NR_KEYS
 * Linux keysyms (K(type, value), <linux/keyboard.h>), indexed by Linux
 * keycode. The keyboard decoder translates through them; KDGKBENT and
 * KDSKBENT read and write them.
 */

/*
 * keymap_get_entry - keysym at @index of table @table. A table that does
 * not exist answers K_NOSUCHMAP at index 0 and K_HOLE elsewhere (KDGKBENT).
 */
unsigned short keymap_get_entry(unsigned int table, unsigned int index);

/*
 * keymap_set_entry - store @value at @index of table @table (KDSKBENT).
 * K_NOSUCHMAP at index 0 drops tables 1..3. Returns 0 or -EINVAL for a
 * value outside its type or a table beyond K_ALTSHIFTTAB.
 */
int keymap_set_entry(unsigned int table, unsigned int index,
		     unsigned short value);

/* Reload every table with layout IR0_KBD_LAYOUT_*; -EINVAL if unknown. */
int keymap_load_layout(int layout);
int keymap_get_layout(void);
