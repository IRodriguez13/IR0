/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_input_mock.c
 * Description: KTM input facade mock — injectable keyboard/mouse, no i8042.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/input_backend.h>
#include <ktm_mock_input.h>
#include <string.h>

#define KTM_INPUT_MOCK_KEY_MAX 32
#define KTM_INPUT_MOCK_PACKET_MAX 4

static char g_keys[KTM_INPUT_MOCK_KEY_MAX];
static unsigned g_key_head;
static unsigned g_key_tail;
static uint8_t g_packets[KTM_INPUT_MOCK_PACKET_MAX][3];
static unsigned g_packet_count;
static ir0_mouse_state_t g_mouse;
static int g_mouse_available;
static int g_sensitivity;
static int g_sensitivity_result = 1;
static int g_layout;
static int g_console_mode;
static int g_mouse_interrupts;
static int g_mouse_feed_bytes;
static uint8_t g_last_aux_byte;
static int g_resync_calls;
static int g_poll_calls;
static int g_wake_calls;

void ktm_input_mock_reset(void)
{
	memset(g_keys, 0, sizeof(g_keys));
	memset(&g_mouse, 0, sizeof(g_mouse));
	memset(g_packets, 0, sizeof(g_packets));
	g_key_head = 0;
	g_key_tail = 0;
	g_packet_count = 0;
	g_mouse_available = 0;
	g_sensitivity = 0;
	g_sensitivity_result = 1;
	g_layout = IR0_KBD_LAYOUT_US;
	g_console_mode = IR0_INPUT_KBD_XLATE;
	g_mouse_interrupts = 0;
	g_mouse_feed_bytes = 0;
	g_last_aux_byte = 0;
	g_resync_calls = 0;
	g_poll_calls = 0;
	g_wake_calls = 0;
}

void ktm_input_mock_set_mouse_available(int on)
{
	g_mouse_available = on ? 1 : 0;
}

void ktm_input_mock_set_mouse_sensitivity_result(int result)
{
	g_sensitivity_result = result;
}

void ktm_input_mock_set_mouse_state(const ir0_mouse_state_t *state)
{
	if (!state)
	{
		memset(&g_mouse, 0, sizeof(g_mouse));
		return;
	}
	g_mouse = *state;
}

void ktm_input_mock_queue_key(char c)
{
	unsigned next = (g_key_head + 1u) % KTM_INPUT_MOCK_KEY_MAX;

	if (next == g_key_tail)
		return; /* Full: drop, like a bounded hardware ring. */
	g_keys[g_key_head] = c;
	g_key_head = next;
}

void ktm_input_mock_queue_mouse_packet(const uint8_t packet[3])
{
	if (!packet || g_packet_count >= KTM_INPUT_MOCK_PACKET_MAX)
		return;
	memcpy(g_packets[g_packet_count], packet, 3);
	g_packet_count++;
}

void ktm_input_mock_set_layout(int layout)
{
	if (layout == IR0_KBD_LAYOUT_US || layout == IR0_KBD_LAYOUT_LATAM)
		g_layout = layout;
}

void ktm_input_mock_set_console_mode(int mode)
{
	g_console_mode = mode;
}

void input_mouse_handle_interrupt(void)
{
	g_mouse_interrupts++;
}

void input_mouse_feed_byte(uint8_t data)
{
	g_mouse_feed_bytes++;
	g_last_aux_byte = data;
}

bool input_mouse_is_available(void)
{
	return g_mouse_available != 0;
}

bool input_mouse_get_state(ir0_mouse_state_t *out)
{
	if (!out || !g_mouse_available)
		return false;
	*out = g_mouse;
	return true;
}

bool input_mouse_set_sensitivity(uint8_t sensitivity)
{
	if (!g_mouse_available)
		return false;
	g_sensitivity = sensitivity;
	return g_sensitivity_result != 0;
}

bool input_mouse_packet_available(void)
{
	return g_mouse_available && g_packet_count > 0;
}

int input_mouse_read_ps2(uint8_t packet[3])
{
	if (!packet || !g_mouse_available || g_packet_count == 0)
		return 0;

	memcpy(packet, g_packets[0], 3);
	g_packet_count--;
	if (g_packet_count)
		memmove(g_packets[0], g_packets[1],
			g_packet_count * sizeof(g_packets[0]));
	return 3;
}

char input_kbd_get(void)
{
	char c;

	if (g_key_tail == g_key_head)
		return 0;
	c = g_keys[g_key_tail];
	g_key_tail = (g_key_tail + 1u) % KTM_INPUT_MOCK_KEY_MAX;
	return c;
}

int input_kbd_has_data(void)
{
	return g_key_tail != g_key_head;
}

void input_kbd_clear(void)
{
	g_key_head = 0;
	g_key_tail = 0;
}

void input_kbd_resync_modifiers(void)
{
	g_resync_calls++;
}

void input_kbd_poll_ps2(void)
{
	g_poll_calls++;
}

int input_kbd_set_layout(int layout)
{
	if (layout != IR0_KBD_LAYOUT_US && layout != IR0_KBD_LAYOUT_LATAM)
		return -1;
	g_layout = layout;
	return 0;
}

int input_kbd_get_layout(void)
{
	return g_layout;
}

const char *input_kbd_get_layout_name(int layout)
{
	if (layout == IR0_KBD_LAYOUT_LATAM)
		return "latam";
	if (layout == IR0_KBD_LAYOUT_US)
		return "us";
	return "unknown";
}

int input_kbd_set_console_mode(int mode)
{
	if (mode != IR0_INPUT_KBD_XLATE && mode != IR0_INPUT_KBD_MEDIUMRAW)
		return -1;
	g_console_mode = mode;
	return 0;
}

int input_kbd_get_console_mode(void)
{
	return g_console_mode;
}

void input_kbd_wake_readers(void)
{
	g_wake_calls++;
}

int ktm_input_mock_mouse_interrupts(void)
{
	return g_mouse_interrupts;
}

int ktm_input_mock_mouse_feed_bytes(void)
{
	return g_mouse_feed_bytes;
}

uint8_t ktm_input_mock_last_aux_byte(void)
{
	return g_last_aux_byte;
}

int ktm_input_mock_sensitivity(void)
{
	return g_sensitivity;
}

int ktm_input_mock_resync_calls(void)
{
	return g_resync_calls;
}

int ktm_input_mock_poll_calls(void)
{
	return g_poll_calls;
}

int ktm_input_mock_wake_calls(void)
{
	return g_wake_calls;
}
