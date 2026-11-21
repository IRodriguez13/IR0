/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: textcon.c
 * Description: Console video backend for a platform text-mode screen.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "console.h"
#include <stddef.h>
#include <stdint.h>

/* Text-mode screen the platform registered; NULL when it has none. */
static const struct console_text_screen *text_screen;

void console_set_text_screen(const struct console_text_screen *screen)
{
	text_screen = screen;
}

const struct console_text_screen *console_text_screen(void)
{
	return text_screen;
}

static void put_cell_text(int row, int col, char c, uint8_t color)
{
	if (!text_screen)
		return;
	if (row < 0 || row >= text_screen->rows || col < 0 || col >= text_screen->cols)
		return;
	text_screen->cells[row * text_screen->cols + col] =
		(uint16_t)((color << 8) | (uint8_t)c);
}

static void scroll_up_text(uint8_t clear_color)
{
	volatile uint16_t *cells;
	int cols;
	int total;
	int i;

	if (!text_screen)
		return;
	cells = text_screen->cells;
	cols = text_screen->cols;
	total = cols * text_screen->rows;
	for (i = 0; i < total - cols; i++)
		cells[i] = cells[i + cols];
	for (; i < total; i++)
		cells[i] = (uint16_t)((clear_color << 8) | ' ');
}

static int cursor_text(int row, int col, int on)
{
	if (!text_screen || !text_screen->cursor)
		return 0;
	text_screen->cursor(row, col, on);
	return 1;
}

static void clear_text(uint8_t color)
{
	int total;
	int i;

	if (!text_screen)
		return;
	total = text_screen->cols * text_screen->rows;
	for (i = 0; i < total; i++)
		text_screen->cells[i] = (uint16_t)((color << 8) | ' ');
}

const struct console_video_ops textcon_video_ops = {
	.is_framebuffer = 0,
	.init = NULL,
	.put_cell = put_cell_text,
	.scroll_up = scroll_up_text,
	.clear = clear_text,
	.cursor = cursor_text,
};
