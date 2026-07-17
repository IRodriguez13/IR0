/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: input_backend.c
 * Description: Input backend — mouse (PS/2) + keyboard buffer facade.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/input_backend.h>
#include <config.h>
#if CONFIG_ENABLE_MOUSE
#include <drivers/IO/ps2_mouse.h>
#endif
#include <interrupt/arch/keyboard.h>

void input_mouse_handle_interrupt(void)
{
#if CONFIG_ENABLE_MOUSE
	ps2_mouse_handle_interrupt();
#endif
}

bool input_mouse_is_available(void)
{
#if CONFIG_ENABLE_MOUSE
	return ps2_mouse_is_available();
#else
	return false;
#endif
}

bool input_mouse_get_state(ir0_mouse_state_t *out)
{
	if (!out)
		return false;
#if CONFIG_ENABLE_MOUSE
	if (!ps2_mouse_is_available())
		return false;
	{
		ps2_mouse_state_t *st = ps2_mouse_get_state();
		if (!st)
			return false;
		out->x = st->x;
		out->y = st->y;
		out->wheel = st->wheel;
		out->left_button = st->left_button;
		out->right_button = st->right_button;
		out->middle_button = st->middle_button;
		out->button4 = st->button4;
		out->button5 = st->button5;
		out->has_wheel = st->has_wheel;
		out->has_5buttons = st->has_5buttons;
		out->resolution = st->resolution;
		out->sample_rate = st->sample_rate;
		out->type = (uint8_t)st->type;
		out->initialized = st->initialized;
	}
	return true;
#else
	return false;
#endif
}

bool input_mouse_set_sensitivity(uint8_t sensitivity)
{
#if CONFIG_ENABLE_MOUSE
	return ps2_mouse_set_sample_rate(sensitivity);
#else
	(void)sensitivity;
	return false;
#endif
}

char input_kbd_get(void)
{
	return keyboard_buffer_get();
}

int input_kbd_has_data(void)
{
	return keyboard_buffer_has_data();
}

void input_kbd_clear(void)
{
	keyboard_buffer_clear();
}

void input_kbd_poll_ps2(void)
{
	keyboard_poll_ps2();
}

int input_kbd_set_layout(int layout)
{
	return keyboard_set_layout(layout);
}

int input_kbd_get_layout(void)
{
	return keyboard_get_layout();
}

const char *input_kbd_get_layout_name(int layout)
{
	return keyboard_get_layout_name(layout);
}
