/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 unified text console renderer — single cursor_pos for /dev/console,
 * TTY echo, and shell write. Minix-style 8x16 cells (scale 1 on fb).
 */

#include "console_renderer.h"
#include "console.h"
#include <ir0/vga.h>
#include <stdint.h>

static uint8_t render_color = CONSOLE_RENDERER_COLOR_DEFAULT;
static int render_cursor_visible;
static int render_cursor_row;
static int render_cursor_col;

static int render_cols(void)
{
	if (console_use_framebuffer())
		return console_get_width();
	return CONSOLE_WIDTH;
}

static int render_rows(void)
{
	if (console_use_framebuffer())
		return console_get_height();
	return CONSOLE_HEIGHT;
}

static void render_erase_cursor(int cols, int rows, uint8_t color)
{
	if (!render_cursor_visible)
		return;
	if (render_cursor_row < 0 || render_cursor_row >= rows)
		return;
	if (render_cursor_col < 0 || render_cursor_col >= cols)
		return;
	console_put_cell(render_cursor_row, render_cursor_col, ' ', color);
	render_cursor_visible = 0;
}

void console_renderer_reset(uint8_t color)
{
	extern int cursor_pos;

	render_color = color;
	render_cursor_visible = 0;
	render_cursor_row = 0;
	render_cursor_col = 0;
	cursor_pos = 0;
	console_clear(color);
}

void console_renderer_putchar(char c, uint8_t color)
{
	extern int cursor_pos;
	int cols = render_cols();
	int rows = render_rows();
	int row;
	int col;

	render_color = color;
	render_erase_cursor(cols, rows, color);

	if (c == '\n')
	{
		cursor_pos = (cursor_pos / cols + 1) * cols;
		if (cursor_pos >= cols * rows)
		{
			console_scroll_up(color);
			cursor_pos = (rows - 1) * cols;
		}
	}
	else if (c == '\r')
	{
		cursor_pos = (cursor_pos / cols) * cols;
	}
	else if (c == '\b' || c == 127)
	{
		if (cursor_pos > 0)
		{
			cursor_pos--;
			row = cursor_pos / cols;
			col = cursor_pos % cols;
			console_put_cell(row, col, ' ', color);
		}
	}
	else if (c == '\t')
	{
		col = cursor_pos % cols;
		{
			int next = (col + 8) & ~7;

			if (next >= cols)
			{
				cursor_pos = (cursor_pos / cols + 1) * cols;
				if (cursor_pos >= cols * rows)
				{
					console_scroll_up(color);
					cursor_pos = (rows - 1) * cols;
				}
			}
			else
			{
				cursor_pos = (cursor_pos / cols) * cols + next;
			}
		}
	}
	else if ((unsigned char)c >= ' ')
	{
		row = cursor_pos / cols;
		col = cursor_pos % cols;
		console_put_cell(row, col, c, color);
		cursor_pos++;
		if (cursor_pos >= cols * rows)
		{
			console_scroll_up(color);
			cursor_pos = (rows - 1) * cols;
		}
	}
}

void console_renderer_show_cursor(uint8_t color)
{
	extern int cursor_pos;
	int cols = render_cols();
	int rows = render_rows();
	int row = cursor_pos / cols;
	int col = cursor_pos % cols;
	uint8_t cur_color;

	render_erase_cursor(cols, rows, color);

	if (row < 0 || row >= rows || col < 0 || col >= cols)
		return;

	cur_color = (uint8_t)(((color & 0xF0) >> 4) | ((color & 0x0F) << 4));
	console_put_cell(row, col, '_', cur_color);
	render_cursor_visible = 1;
	render_cursor_row = row;
	render_cursor_col = col;
}

int console_renderer_get_cursor_x(void)
{
	extern int cursor_pos;
	int cols = render_cols();

	if (cols <= 0)
		return 0;
	return cursor_pos % cols;
}

int console_renderer_get_cursor_y(void)
{
	extern int cursor_pos;
	int cols = render_cols();

	if (cols <= 0)
		return 0;
	return cursor_pos / cols;
}

void console_renderer_import_cursor_pos(int cols)
{
	extern int cursor_pos;

	if (cols <= 0)
		return;
	if (cursor_pos < 0)
		cursor_pos = 0;
}
