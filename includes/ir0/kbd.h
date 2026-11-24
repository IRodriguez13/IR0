/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: kbd.h
 * Description: Console keyboard, Linux drivers/tty/vt/keyboard.c model. An
 *              input handler attached to every keyboard turns EV_KEY into
 *              console bytes (keymap, modifiers, locks, CSI for the cursor
 *              keys, K_MEDIUMRAW codes). The console line discipline takes
 *              them from here; no keyboard driver is ever named.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/* Momentary modifiers and lock toggles of the console keyboard. */
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

int kbd_mods_ctrl(const struct keyboard_modifiers *m);
int kbd_mods_shift(const struct keyboard_modifiers *m);
uint32_t kbd_mods_mask(const struct keyboard_modifiers *m);
/* Drop the momentary modifiers; the locks stay. */
void kbd_mods_release(struct keyboard_modifiers *m);

/*
 * One EV_KEY (value 0 release, 1 press, 2 autorepeat) through the keymap:
 * updates @m and writes the console bytes to @out. Returns their count.
 */
size_t kbd_translate(struct keyboard_modifiers *m, unsigned int keycode,
		     int value, uint8_t out[8]);

/* Console keyboard mode (console_ioctl(2) KDSKBMODE). */
#define KBD_MODE_XLATE		0
#define KBD_MODE_MEDIUMRAW	2
int kbd_set_mode(int mode);
int kbd_get_mode(void);

/* Keymap layouts (IR0 keymap syscalls). */
#define KBD_LAYOUT_US		0
#define KBD_LAYOUT_LATAM	1
int kbd_set_layout(int layout);
int kbd_get_layout(void);

/* The raw (!ICANON) console queue the handler fills. */
char kbd_getc(void);
int kbd_has_data(void);
void kbd_flush(void);
/* Forget held modifiers (session end, TCSETSF, overflow). */
void kbd_reset(void);
/* Take @c as if it had been typed (TIOCSTI). */
void kbd_receive(char c);

int kbd_init(void);
