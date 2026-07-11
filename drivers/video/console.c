/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: console.c
 * Description: Unified VGA text + VBE framebuffer console (FASE59B aesthetics).
 */

#include "console.h"
#include "console_font.h"
#include <config.h>
#if CONFIG_ENABLE_VBE
#include <drivers/video/vbe.h>
#endif
#include <ir0/serial_io.h>
#include <stdbool.h>
#include <string.h>

static int use_fb = 0;
static int fb_console_cols = CONSOLE_WIDTH;
static int fb_console_rows = CONSOLE_HEIGHT;
static int fb_scale = CONSOLE_FB_SCALE_DEFAULT;
static int fb_origin_x;
static int fb_origin_y;
static int fb_cell_w;
static int fb_cell_h;

#define VGA_BUF ((volatile uint16_t *)0xB8000)

static void put_cell_vga(int row, int col, char c, uint8_t color)
{
	if (row < 0 || row >= CONSOLE_HEIGHT || col < 0 || col >= CONSOLE_WIDTH)
		return;
	VGA_BUF[row * CONSOLE_WIDTH + col] = (uint16_t)((color << 8) | (uint8_t)c);
}

static void scroll_up_vga(uint8_t clear_color)
{
	int i;

	for (i = 0; i < (CONSOLE_HEIGHT - 1) * CONSOLE_WIDTH; i++)
		VGA_BUF[i] = VGA_BUF[i + CONSOLE_WIDTH];
	for (i = (CONSOLE_HEIGHT - 1) * CONSOLE_WIDTH; i < CONSOLE_HEIGHT * CONSOLE_WIDTH; i++)
		VGA_BUF[i] = (uint16_t)((clear_color << 8) | ' ');
}

static void clear_vga(uint8_t color)
{
	int i;

	for (i = 0; i < CONSOLE_WIDTH * CONSOLE_HEIGHT; i++)
		VGA_BUF[i] = (uint16_t)((color << 8) | ' ');
}

#if CONFIG_ENABLE_VBE
static const uint8_t vga_palette_rgb[16][3] = {
	{0, 0, 0}, {0, 0, 170}, {0, 170, 0}, {0, 170, 170},
	{170, 0, 0}, {170, 0, 170}, {170, 85, 0}, {170, 170, 170},
	{85, 85, 85}, {85, 85, 255}, {85, 255, 85}, {85, 255, 255},
	{255, 85, 85}, {255, 85, 255}, {255, 255, 85}, {255, 255, 255}
};

static uint32_t fb_rgb_from_vga(uint8_t idx)
{
	return vbe_rgb_to_pixel(vga_palette_rgb[idx][0],
				vga_palette_rgb[idx][1],
				vga_palette_rgb[idx][2]);
}

static void fb_fill_rect(int x, int y, int w, int h, uint32_t rgb, uint32_t pitch,
			 uint8_t *fb)
{
	int dy;
	int dx;

	for (dy = 0; dy < h; dy++)
	{
		uint32_t *line = (uint32_t *)(fb + (y + dy) * pitch);

		for (dx = 0; dx < w; dx++)
			line[x + dx] = rgb;
	}
}

static void fb_fill_border(uint32_t w, uint32_t h, uint32_t pitch, uint8_t *fb)
{
	uint32_t border = fb_rgb_from_vga(CONSOLE_FB_BORDER_COLOR & 0x0F);
	size_t i;
	size_t pixels = (pitch * h) / 4;
	uint32_t *p = (uint32_t *)fb;

	for (i = 0; i < pixels; i++)
		p[i] = border;
}

static void fb_clear_console_region(uint8_t color, uint32_t pitch, uint8_t *fb)
{
	int pw = fb_console_cols * fb_cell_w;
	int ph = fb_console_rows * fb_cell_h;
	uint8_t bg = (color >> 4) & 0x0F;
	uint32_t bg_rgb = fb_rgb_from_vga(bg);

	fb_fill_rect(fb_origin_x, fb_origin_y, pw, ph, bg_rgb, pitch, fb);
}

static void fb_compute_layout(uint32_t w, uint32_t h)
{
	int pw;
	int ph;

	fb_console_cols = CONSOLE_WIDTH;
	fb_console_rows = CONSOLE_HEIGHT;
	fb_scale = CONSOLE_FB_SCALE_DEFAULT;
	fb_cell_w = FONT_WIDTH * fb_scale;
	fb_cell_h = FONT_HEIGHT * fb_scale;
	pw = fb_console_cols * fb_cell_w;
	ph = fb_console_rows * fb_cell_h;

	if (pw > (int)w || ph > (int)h)
	{
		fb_scale = 1;
		fb_cell_w = FONT_WIDTH;
		fb_cell_h = FONT_HEIGHT;
		pw = fb_console_cols * fb_cell_w;
		ph = fb_console_rows * fb_cell_h;
	}

	fb_origin_x = ((int)w - pw) / 2;
	fb_origin_y = ((int)h - ph) / 2;
	if (fb_origin_x < 0)
		fb_origin_x = 0;
	if (fb_origin_y < 0)
		fb_origin_y = 0;
}

static void put_cell_fb(int row, int col, char c, uint8_t color)
{
	uint32_t w;
	uint32_t h;
	uint32_t bpp;
	uint32_t pitch;
	uint8_t *fb;
	uint32_t fg_rgb;
	uint32_t bg_rgb;
	int px;
	int py;
	unsigned char idx;
	const unsigned char *glyph;
	int dy;
	int sy;
	int dx;
	int sx;

	if (row < 0 || row >= fb_console_rows || col < 0 || col >= fb_console_cols)
		return;

	if (!vbe_get_info(&w, &h, &bpp) || bpp != 32)
		return;

	pitch = vbe_get_pitch();
	fb = vbe_get_fb();
	if (!fb)
		return;

	fg_rgb = fb_rgb_from_vga(color & 0x0F);
	bg_rgb = fb_rgb_from_vga((color >> 4) & 0x0F);
	px = fb_origin_x + col * fb_cell_w;
	py = fb_origin_y + row * fb_cell_h;

	if (px + fb_cell_w > (int)w || py + fb_cell_h > (int)h)
		return;

	idx = (unsigned char)c;
	glyph = font_8x16[idx];

	for (dy = 0; dy < FONT_HEIGHT; dy++)
	{
		uint8_t row_bits = glyph[dy];

		for (sy = 0; sy < fb_scale; sy++)
		{
			uint32_t *line = (uint32_t *)(fb + (py + dy * fb_scale + sy) * pitch);

			for (dx = 0; dx < FONT_WIDTH; dx++)
			{
				uint32_t pixel = (row_bits & (0x80 >> dx)) ? fg_rgb : bg_rgb;

				for (sx = 0; sx < fb_scale; sx++)
					line[px + dx * fb_scale + sx] = pixel;
			}
		}
	}
}

static void scroll_up_fb(uint8_t clear_color)
{
	uint32_t w;
	uint32_t h;
	uint32_t bpp;
	uint32_t pitch;
	uint8_t *fb;
	int pw;
	int ph;
	int row;
	int dy;
	uint32_t bg_rgb;

	if (!vbe_get_info(&w, &h, &bpp) || bpp != 32)
		return;

	pitch = vbe_get_pitch();
	fb = vbe_get_fb();
	if (!fb)
		return;

	pw = fb_console_cols * fb_cell_w;
	ph = fb_console_rows * fb_cell_h;
	bg_rgb = fb_rgb_from_vga((clear_color >> 4) & 0x0F);

	for (row = 1; row < fb_console_rows; row++)
	{
		for (dy = 0; dy < fb_cell_h; dy++)
		{
			uint32_t *dst = (uint32_t *)(fb +
				(fb_origin_y + (row - 1) * fb_cell_h + dy) * pitch) +
				fb_origin_x;
			uint32_t *src = (uint32_t *)(fb +
				(fb_origin_y + row * fb_cell_h + dy) * pitch) +
				fb_origin_x;

			memmove(dst, src, (size_t)pw * sizeof(uint32_t));
		}
	}

	fb_fill_rect(fb_origin_x,
		     fb_origin_y + (fb_console_rows - 1) * fb_cell_h,
		     pw, fb_cell_h, bg_rgb, pitch, fb);
}

static void clear_fb(uint8_t color)
{
	uint32_t w;
	uint32_t h;
	uint32_t bpp;
	uint32_t pitch;
	uint8_t *fb;

	if (!vbe_get_info(&w, &h, &bpp) || bpp != 32)
		return;

	pitch = vbe_get_pitch();
	fb = vbe_get_fb();
	if (!fb)
		return;

	fb_fill_border(w, h, pitch, fb);
	fb_clear_console_region(color, pitch, fb);
}
#else
static void put_cell_fb(int row, int col, char c, uint8_t color)
{
	(void)row;
	(void)col;
	(void)c;
	(void)color;
}

static void scroll_up_fb(uint8_t clear_color)
{
	(void)clear_color;
}

static void clear_fb(uint8_t color)
{
	(void)color;
}
#endif

void console_put_cell(int row, int col, char c, uint8_t color)
{
	if (use_fb)
		put_cell_fb(row, col, c, color);
	else
		put_cell_vga(row, col, c, color);
}

void console_scroll_up(uint8_t clear_color)
{
	if (use_fb)
		scroll_up_fb(clear_color);
	else
		scroll_up_vga(clear_color);
}

void console_clear(uint8_t color)
{
	if (use_fb)
		clear_fb(color);
	else
		clear_vga(color);
}

void console_init(void)
{
#if CONFIG_ENABLE_VBE
	uint32_t w;
	uint32_t h;
	uint32_t bpp;

	use_fb = 0;
	if (vbe_is_available() && vbe_get_info(&w, &h, &bpp) && bpp == 32 &&
	    w >= 640 && h >= 400)
	{
		use_fb = 1;
		fb_compute_layout(w, h);
		serial_print("CONSOLE_FB_BACKEND_ENABLED\n");
	}
#else
	use_fb = 0;
#endif
}

int console_use_framebuffer(void)
{
	return use_fb;
}

int console_get_width(void)
{
	if (use_fb)
		return fb_console_cols;
	return CONSOLE_WIDTH;
}

int console_get_height(void)
{
	if (use_fb)
		return fb_console_rows;
	return CONSOLE_HEIGHT;
}

int console_get_fb_scale(void)
{
	if (use_fb)
		return fb_scale;
	return 1;
}
