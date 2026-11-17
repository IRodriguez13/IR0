/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: input_backend.h
 * Description: Input facade (mouse + keyboard) — no drivers/ in callers.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define IR0_KBD_LAYOUT_US    0
#define IR0_KBD_LAYOUT_LATAM 1

typedef struct ir0_mouse_state
{
	int32_t x;
	int32_t y;
	int8_t wheel;
	bool left_button;
	bool right_button;
	bool middle_button;
	bool button4;
	bool button5;
	bool has_wheel;
	bool has_5buttons;
	uint8_t resolution;
	uint8_t sample_rate;
	uint8_t type;
	bool initialized;
} ir0_mouse_state_t;

/*
 * Keyboard and mouse drivers register their operations; the facade below
 * forwards to them and reports no device while none is registered.
 */
struct input_keyboard_ops
{
	char (*get)(void);
	int (*has_data)(void);
	void (*clear)(void);
	/* Drop half-decoded scancode state (momentary mods + E0/E1). */
	void (*all_keys_up)(void);
	/* Drain the controller when its IRQ may have been missed. */
	void (*poll)(void);
	int (*set_layout)(int layout);
	int (*get_layout)(void);
	const char *(*get_layout_name)(int layout);
	int (*set_console_mode)(int mode);
	int (*get_console_mode)(void);
	/* Take @c as if it had just been typed (TIOCSTI). */
	void (*receive)(char c);
};

struct input_mouse_ops
{
	void (*feed_byte)(uint8_t data);
	bool (*is_available)(void);
	bool (*get_state)(ir0_mouse_state_t *out);
	bool (*set_sample_rate)(uint8_t rate);
	bool (*packet_available)(void);
	/* One standard three-byte PS/2 packet; returns its length or 0. */
	int (*read_packet)(uint8_t packet[3]);
};

void input_register_keyboard(const struct input_keyboard_ops *ops);
void input_register_mouse(const struct input_mouse_ops *ops);

void input_mouse_handle_interrupt(void);
/* One AUX byte from i8042 (never fed to the keyboard decoder). */
void input_mouse_feed_byte(uint8_t data);
bool input_mouse_is_available(void);
bool input_mouse_get_state(ir0_mouse_state_t *out);
bool input_mouse_set_sensitivity(uint8_t sensitivity);
/* Linux /dev/mouse compatibility: one standard three-byte PS/2 packet. */
bool input_mouse_packet_available(void);
int input_mouse_read_ps2(uint8_t packet[3]);

char input_kbd_get(void);
int input_kbd_has_data(void);
void input_kbd_clear(void);
/* Drop half-decoded scancode state (momentary mods + E0/E1); keep the ring. */
void input_kbd_resync_modifiers(void);
void input_kbd_poll_ps2(void);
int input_kbd_set_layout(int layout);
int input_kbd_get_layout(void);
const char *input_kbd_get_layout_name(int layout);

/* Linux console keyboard discipline used by unmodified userspace (Xfbdev). */
#define IR0_INPUT_KBD_XLATE     0
#define IR0_INPUT_KBD_MEDIUMRAW 2
int input_kbd_set_console_mode(int mode);
int input_kbd_get_console_mode(void);
/* Feed @c to the keyboard path as a typed byte; -1 without a keyboard. */
int input_kbd_receive(char c);
/* Publish newly queued console bytes to poll/select waiters. */
void input_kbd_wake_readers(void);
