/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 unified text console renderer — single cursor_pos for /dev/console,
 * TTY echo, and shell write. Minix-style 8x16 cells (scale 1 on fb).
 */

#include "console_renderer.h"
#include "console.h"
#include <ir0/console.h>
#include <ir0/serial_io.h>
#include <ir0/vga.h>
#include <stdint.h>

#define CSI_NONE    0
#define CSI_ESC     1
#define CSI_BRACKET 2

static uint8_t render_color = CONSOLE_RENDERER_COLOR_DEFAULT;
static int render_cursor_visible;
static int render_cursor_row;
static int render_cursor_col;
static int csi_state;
static int csi_param;
static int csi_params[4];
static int csi_param_count;

static void csi_reset(void)
{
	csi_state = CSI_NONE;
	csi_param = 0;
	csi_param_count = 0;
}

static void csi_clear_from_cursor(int cols, int rows, uint8_t color)
{
	extern int cursor_pos;
	int row = cursor_pos / cols;
	int col = cursor_pos % cols;
	int r;
	int c;

	for (r = row; r < rows; r++)
	{
		int start = (r == row) ? col : 0;

		for (c = start; c < cols; c++)
			console_put_cell(r, c, ' ', color);
	}
}

static void csi_clear_screen(int cols, int rows, uint8_t color)
{
	int r;
	int c;

	for (r = 0; r < rows; r++)
		for (c = 0; c < cols; c++)
			console_put_cell(r, c, ' ', color);
}

static void csi_apply(char cmd, int cols, int rows, uint8_t color)
{
	extern int cursor_pos;
	int n = 1;
	int row;
	int col;
	int i;

	if (csi_param_count > 0)
		n = csi_params[0];
	if (n <= 0)
		n = 1;

	switch (cmd)
	{
	case 'A':
		if (cursor_pos >= n * cols)
			cursor_pos -= n * cols;
		else
			cursor_pos = (cursor_pos / cols) * cols;
		break;
	case 'J':
	{
		int mode = csi_param_count > 0 ? csi_params[0] : 0;

		if (mode == 2)
		{
			csi_clear_screen(cols, rows, color);
			cursor_pos = 0;
		}
		else
			csi_clear_from_cursor(cols, rows, color);
		break;
	}
	case 'D':
		if (cursor_pos >= n)
			cursor_pos -= n;
		else
			cursor_pos = (cursor_pos / cols) * cols;
		break;
	case 'C':
		cursor_pos += n;
		if (cursor_pos >= cols * rows)
			cursor_pos = cols * rows - 1;
		break;
	case 'K':
	{
		int mode = csi_param_count > 0 ? csi_params[0] : 0;

		row = cursor_pos / cols;
		if (mode == 2)
		{
			for (i = 0; i < cols; i++)
				console_put_cell(row, i, ' ', color);
		}
		else if (mode == 1)
		{
			col = cursor_pos % cols;
			for (i = 0; i <= col; i++)
				console_put_cell(row, i, ' ', color);
		}
		else
		{
			col = cursor_pos % cols;
			for (i = col; i < cols; i++)
				console_put_cell(row, i, ' ', color);
		}
		break;
	}
	case 'G':
		col = (csi_param_count > 0 ? csi_params[0] : 1) - 1;
		if (col < 0)
			col = 0;
		if (col >= cols)
			col = cols - 1;
		cursor_pos = (cursor_pos / cols) * cols + col;
		break;
	case 'H':
	case 'f':
	{
		int crow = 1;
		int ccol = 1;

		if (csi_param_count >= 1)
			crow = csi_params[0];
		if (csi_param_count >= 2)
			ccol = csi_params[1];
		if (crow < 1)
			crow = 1;
		if (ccol < 1)
			ccol = 1;
		crow--;
		ccol--;
		if (crow >= rows)
			crow = rows - 1;
		if (ccol >= cols)
			ccol = cols - 1;
		cursor_pos = crow * cols + ccol;
		break;
	}
	default:
		break;
	}
}

static int csi_feed(char c, int cols, int rows, uint8_t color)
{
	if (csi_state == CSI_ESC)
	{
		if (c == '[')
		{
			csi_state = CSI_BRACKET;
			csi_param = 0;
			csi_param_count = 0;
			return 1;
		}
		csi_reset();
		return 0;
	}

	if (csi_state == CSI_BRACKET)
	{
		if (c >= '0' && c <= '9')
		{
			csi_param = csi_param * 10 + (c - '0');
			return 1;
		}
		if (c == ';')
		{
			if (csi_param_count < 4)
				csi_params[csi_param_count++] = csi_param;
			csi_param = 0;
			return 1;
		}
		if (c == '?')
			return 1;
		if (c >= 0x40 && c <= 0x7e)
		{
			if (csi_param_count < 4)
				csi_params[csi_param_count++] = csi_param;
			csi_apply(c, cols, rows, color);
			csi_reset();
			return 1;
		}
		csi_reset();
		return 0;
	}

	if ((unsigned char)c == 0x1b)
	{
		csi_state = CSI_ESC;
		return 1;
	}

	return 0;
}

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

	if (csi_feed(c, cols, rows, color))
		return;

	render_erase_cursor(cols, rows, color);

	if (c == '\n')
	{
		csi_reset();
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

	if (ir0_console_in_userspace())
		return;

	serial_print("FB_CURSOR_POS x=");
	serial_print_hex32((uint32_t)col);
	serial_print(" y=");
	serial_print_hex32((uint32_t)row);
	serial_print("\n");

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
