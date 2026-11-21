/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: console.c
 * Description: Console core: cell shadow, geometry and video backend dispatch.
 */

#include "console.h"
#include "console_font.h"
#include <config.h>
#include <ir0/serial_io.h>
#include <stdbool.h>
#include <stddef.h>
#include <ir0/ktm/klog.h>

#define CONSOLE_TEXT_GEOMETRY                                      \
	{                                                          \
		.pixel_width = CONSOLE_WIDTH * FONT_EARLY_WIDTH,   \
		.pixel_height = CONSOLE_HEIGHT * FONT_EARLY_HEIGHT, \
		.cell_width = FONT_EARLY_WIDTH,                    \
		.cell_height = FONT_EARLY_HEIGHT,                  \
		.columns = CONSOLE_WIDTH,                          \
		.rows = CONSOLE_HEIGHT,                            \
		.scale = 1,                                        \
	}

static const struct console_geometry console_text_geometry =
	CONSOLE_TEXT_GEOMETRY;

/* Probed in order; the first backend that accepts the screen draws it. */
static const struct console_video_ops *const console_video_backends[] = {
#if CONFIG_ENABLE_VBE
	&fbcon_video_ops,
#endif
	&textcon_video_ops,
};

static const struct console_video_ops *video = &textcon_video_ops;
static struct console_geometry geometry = CONSOLE_TEXT_GEOMETRY;

/* Glyph+color shadow so the software cursor can restore column 0 after CR/LF. */
static uint16_t cell_shadow[CONSOLE_MAX_HEIGHT][CONSOLE_MAX_WIDTH];

static int shadow_rows(void)
{
	return (int)geometry.rows;
}

static int shadow_cols(void)
{
	return (int)geometry.columns;
}

static void shadow_put(int row, int col, char c, uint8_t color)
{
	int rows = shadow_rows();
	int cols = shadow_cols();

	if (row < 0 || row >= rows || col < 0 || col >= cols)
		return;
	if (row >= CONSOLE_MAX_HEIGHT || col >= CONSOLE_MAX_WIDTH)
		return;
	cell_shadow[row][col] = (uint16_t)(((uint16_t)color << 8) | (uint8_t)c);
}

static void shadow_scroll_up(uint8_t clear_color)
{
	int r;
	int c;
	int rows = shadow_rows();
	int cols = shadow_cols();

	if (rows > CONSOLE_MAX_HEIGHT)
		rows = CONSOLE_MAX_HEIGHT;
	if (cols > CONSOLE_MAX_WIDTH)
		cols = CONSOLE_MAX_WIDTH;

	for (r = 0; r < rows - 1; r++)
	{
		for (c = 0; c < cols; c++)
			cell_shadow[r][c] = cell_shadow[r + 1][c];
	}
	for (c = 0; c < cols; c++)
		cell_shadow[rows - 1][c] =
			(uint16_t)(((uint16_t)clear_color << 8) | ' ');
}

static void shadow_clear(uint8_t color)
{
	int r;
	int c;
	int rows = shadow_rows();
	int cols = shadow_cols();

	if (rows > CONSOLE_MAX_HEIGHT)
		rows = CONSOLE_MAX_HEIGHT;
	if (cols > CONSOLE_MAX_WIDTH)
		cols = CONSOLE_MAX_WIDTH;

	for (r = 0; r < rows; r++)
	{
		for (c = 0; c < cols; c++)
			cell_shadow[r][c] = (uint16_t)(((uint16_t)color << 8) | ' ');
	}
}

void console_get_geometry(struct console_geometry *geo)
{
	if (geo)
		*geo = geometry;
}

void console_draw_cell(int row, int col, char c, uint8_t color)
{
	video->put_cell(row, col, c, color);
}

void console_put_cell(int row, int col, char c, uint8_t color)
{
	shadow_put(row, col, c, color);
	console_draw_cell(row, col, c, color);
}

uint16_t console_get_cell(int row, int col)
{
	int rows = shadow_rows();
	int cols = shadow_cols();

	if (row < 0 || row >= rows || col < 0 || col >= cols)
		return (uint16_t)((0x07u << 8) | ' ');
	if (row >= CONSOLE_MAX_HEIGHT || col >= CONSOLE_MAX_WIDTH)
		return (uint16_t)((0x07u << 8) | ' ');
	return cell_shadow[row][col];
}

void console_scroll_up(uint8_t clear_color)
{
	shadow_scroll_up(clear_color);
	video->scroll_up(clear_color);
}

void console_clear(uint8_t color)
{
	shadow_clear(color);
	video->clear(color);
}

void console_init(void)
{
	size_t i;

	video = &textcon_video_ops;
	geometry = console_text_geometry;
	for (i = 0; i < sizeof(console_video_backends) /
			sizeof(console_video_backends[0]); i++)
	{
		const struct console_video_ops *ops = console_video_backends[i];
		struct console_geometry geo = console_text_geometry;

		if (ops->init && ops->init(&geo) != 0)
			continue;
		video = ops;
		geometry = geo;
		break;
	}
	if (video->is_framebuffer)
		klog_smoke("CONSOLE_FB_BACKEND_ENABLED");
}

int console_use_framebuffer(void)
{
	return video->is_framebuffer;
}

int console_get_width(void)
{
	return (int)geometry.columns;
}

int console_get_height(void)
{
	return (int)geometry.rows;
}

int console_get_fb_scale(void)
{
	return (int)geometry.scale;
}
