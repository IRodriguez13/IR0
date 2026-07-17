/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: keyboard.h
 * Description: Thin compat wrappers — prefer <ir0/input_backend.h>.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef _IR0_KEYBOARD_H
#define _IR0_KEYBOARD_H

#include <ir0/input_backend.h>

#define KEYBOARD_LAYOUT_US    IR0_KBD_LAYOUT_US
#define KEYBOARD_LAYOUT_LATAM IR0_KBD_LAYOUT_LATAM

static inline char keyboard_buffer_get(void)
{
	return input_kbd_get();
}

static inline int keyboard_buffer_has_data(void)
{
	return input_kbd_has_data();
}

static inline void keyboard_buffer_clear(void)
{
	input_kbd_clear();
}

static inline int keyboard_set_layout(int layout)
{
	return input_kbd_set_layout(layout);
}

static inline int keyboard_get_layout(void)
{
	return input_kbd_get_layout();
}

static inline const char *keyboard_get_layout_name(int layout)
{
	return input_kbd_get_layout_name(layout);
}

#endif /* _IR0_KEYBOARD_H */
