/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: input_backend.c
 * Description: Input facade — forwards to the registered keyboard and mouse drivers.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/input_backend.h>
#include <ir0/poll.h>
#include <stddef.h>

/*
 * The keyboard and mouse drivers a platform has register here. While none is
 * registered the facade reports no device: no keys, no mouse, no layouts.
 */
static const struct input_keyboard_ops *keyboard;
static const struct input_mouse_ops *mouse;

void input_register_keyboard(const struct input_keyboard_ops *ops)
{
	keyboard = ops;
}

void input_register_mouse(const struct input_mouse_ops *ops)
{
	mouse = ops;
}

void input_mouse_feed_byte(uint8_t data)
{
	/* Without a mouse driver the AUX byte is discarded. */
	if (mouse)
		mouse->feed_byte(data);
}

void input_mouse_handle_interrupt(void)
{
	/*
	 * IRQ12 and IRQ1 share the i8042 output buffer. Always demux by
	 * status AUXDATA — never assume this IRQ's byte is mouse-only.
	 */
	input_kbd_poll_ps2();
}

bool input_mouse_is_available(void)
{
	return mouse && mouse->is_available();
}

bool input_mouse_get_state(ir0_mouse_state_t *out)
{
	if (!out || !mouse)
		return false;
	return mouse->get_state(out);
}

bool input_mouse_set_sensitivity(uint8_t sensitivity)
{
	return mouse && mouse->set_sample_rate(sensitivity);
}

bool input_mouse_packet_available(void)
{
	return mouse && mouse->packet_available();
}

int input_mouse_read_ps2(uint8_t packet[3])
{
	if (!packet || !mouse)
		return 0;
	return mouse->read_packet(packet);
}

char input_kbd_get(void)
{
	return keyboard ? keyboard->get() : 0;
}

int input_kbd_has_data(void)
{
	return keyboard ? keyboard->has_data() : 0;
}

void input_kbd_clear(void)
{
	if (keyboard)
		keyboard->clear();
}

void input_kbd_resync_modifiers(void)
{
	if (keyboard)
		keyboard->all_keys_up();
}

void input_kbd_poll_ps2(void)
{
	if (keyboard)
		keyboard->poll();
}

int input_kbd_set_layout(int layout)
{
	return keyboard ? keyboard->set_layout(layout) : -1;
}

int input_kbd_get_layout(void)
{
	return keyboard ? keyboard->get_layout() : IR0_KBD_LAYOUT_US;
}

const char *input_kbd_get_layout_name(int layout)
{
	return keyboard ? keyboard->get_layout_name(layout) : "";
}

int input_kbd_set_console_mode(int mode)
{
	return keyboard ? keyboard->set_console_mode(mode) : -1;
}

int input_kbd_get_console_mode(void)
{
	return keyboard ? keyboard->get_console_mode() : IR0_INPUT_KBD_XLATE;
}

int input_kbd_receive(char c)
{
	if (!keyboard || !keyboard->receive)
		return -1;
	keyboard->receive(c);
	return 0;
}

void input_kbd_wake_readers(void)
{
	(void)poll_wake_check_nosched();
}
