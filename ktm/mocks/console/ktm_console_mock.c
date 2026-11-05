/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_console_mock.c
 * Description: KTM console facade mock — records output, touches no hardware.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/console_backend.h>
#include <ktm_mock_console.h>

#define KTM_CONSOLE_MOCK_TEXT_MAX 4096u

static char g_text[KTM_CONSOLE_MOCK_TEXT_MAX];
static char g_serial_text[KTM_CONSOLE_MOCK_TEXT_MAX];
static size_t g_text_len;
static size_t g_serial_len;
static size_t g_write_bytes;
static size_t g_serial_bytes;
static int g_framebuffer;
static int g_fb_scale = 1;
static int g_cursor_x;
static int g_printk_to_screen = 1;
static int g_serial_mirror = 1;
static int g_init_calls;
static int g_typewriter_init_calls;
static int g_clear_calls;
static int g_scroll_lines;
static int g_cursor_shows;
static int g_handoff_calls;
static int g_panic_screen_calls;
static uint8_t g_last_color = IR0_CONSOLE_COLOR_DEFAULT;

static void mock_append(char *dst, size_t *dst_len, const char *src, size_t len)
{
	size_t room;
	size_t copy;

	if (!dst || !dst_len || !src)
		return;

	room = KTM_CONSOLE_MOCK_TEXT_MAX - 1u - *dst_len;
	copy = len < room ? len : room;
	if (copy)
	{
		size_t i;

		for (i = 0; i < copy; i++)
			dst[*dst_len + i] = src[i];
		*dst_len += copy;
		dst[*dst_len] = '\0';
	}
}

void ktm_console_mock_reset(void)
{
	size_t i;

	for (i = 0; i < KTM_CONSOLE_MOCK_TEXT_MAX; i++)
	{
		g_text[i] = '\0';
		g_serial_text[i] = '\0';
	}
	g_text_len = 0;
	g_serial_len = 0;
	g_write_bytes = 0;
	g_serial_bytes = 0;
	g_framebuffer = 0;
	g_fb_scale = 1;
	g_cursor_x = 0;
	g_printk_to_screen = 1;
	g_serial_mirror = 1;
	g_init_calls = 0;
	g_typewriter_init_calls = 0;
	g_clear_calls = 0;
	g_scroll_lines = 0;
	g_cursor_shows = 0;
	g_handoff_calls = 0;
	g_panic_screen_calls = 0;
	g_last_color = IR0_CONSOLE_COLOR_DEFAULT;
}

void ktm_console_mock_set_framebuffer(int on)
{
	g_framebuffer = on ? 1 : 0;
}

void ktm_console_mock_set_fb_scale(int scale)
{
	g_fb_scale = scale;
}

void ktm_console_mock_set_cursor_x(int x)
{
	g_cursor_x = x;
}

void ktm_console_mock_set_printk_to_screen(int on)
{
	g_printk_to_screen = on ? 1 : 0;
}

void ktm_console_mock_set_serial_mirror(int on)
{
	g_serial_mirror = on ? 1 : 0;
}

void console_backend_init(void)
{
	g_init_calls++;
}

void console_backend_typewriter_init(void)
{
	g_typewriter_init_calls++;
}

void console_backend_clear(uint8_t color)
{
	g_clear_calls++;
	g_last_color = color;
}

int console_backend_uses_framebuffer(void)
{
	return g_framebuffer;
}

int console_backend_fb_scale(void)
{
	return g_fb_scale;
}

void console_backend_scroll(int lines)
{
	g_scroll_lines += lines;
}

void console_backend_write(const char *str, size_t len, uint8_t color)
{
	if (!str)
		return;
	g_write_bytes += len;
	g_last_color = color;
	mock_append(g_text, &g_text_len, str, len);
}

void console_backend_write_serial(const char *str, size_t len)
{
	if (!str || !g_serial_mirror)
		return;
	g_serial_bytes += len;
	mock_append(g_serial_text, &g_serial_len, str, len);
}

void console_backend_show_cursor(uint8_t color)
{
	g_cursor_shows++;
	g_last_color = color;
}

int console_backend_cursor_x(void)
{
	return g_cursor_x;
}

void console_backend_userspace_handoff(void)
{
	g_handoff_calls++;
	g_printk_to_screen = 0;
}

void console_backend_set_tty_serial_mirror(int on)
{
	g_serial_mirror = on ? 1 : 0;
}

int console_backend_printk_to_screen(void)
{
	return g_printk_to_screen;
}

void console_backend_panic_screen_on(void)
{
	g_panic_screen_calls++;
	g_printk_to_screen = 1;
}

size_t ktm_console_mock_write_bytes(void)
{
	return g_write_bytes;
}

size_t ktm_console_mock_serial_bytes(void)
{
	return g_serial_bytes;
}

const char *ktm_console_mock_text(void)
{
	return g_text;
}

const char *ktm_console_mock_serial_text(void)
{
	return g_serial_text;
}

int ktm_console_mock_init_calls(void)
{
	return g_init_calls;
}

int ktm_console_mock_typewriter_init_calls(void)
{
	return g_typewriter_init_calls;
}

int ktm_console_mock_clear_calls(void)
{
	return g_clear_calls;
}

int ktm_console_mock_scroll_lines(void)
{
	return g_scroll_lines;
}

int ktm_console_mock_cursor_shows(void)
{
	return g_cursor_shows;
}

int ktm_console_mock_handoff_calls(void)
{
	return g_handoff_calls;
}

int ktm_console_mock_panic_screen_calls(void)
{
	return g_panic_screen_calls;
}

uint8_t ktm_console_mock_last_color(void)
{
	return g_last_color;
}
