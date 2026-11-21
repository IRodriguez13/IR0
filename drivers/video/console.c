/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: console.c
 * Description: Unified text-mode screen + VBE framebuffer console.
 */

#include "console.h"
#include "console_font.h"
#include <config.h>
#if CONFIG_ENABLE_VBE
#include <ir0/vbe.h>
#endif
#include <ir0/serial_io.h>
#include <stdbool.h>
#include <string.h>
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
};

#if CONFIG_ENABLE_VBE
static int fb_console_cols = CONSOLE_WIDTH;
static int fb_console_rows = CONSOLE_HEIGHT;
static int fb_scale = CONSOLE_FB_SCALE_DEFAULT;
static int fb_cell_w;
static int fb_cell_h;
static int fb_origin_x;
static int fb_origin_y;

/*
 * Soft product palette: off-black bg, light-gray fg (not pure white).
 * Blues are lifted so ANSI di/title colors stay readable on the dark bg
 * (classic VGA 0,0,170 on #0c0c0e is nearly invisible).
 */
static const uint8_t vga_palette_rgb[16][3] = {
	{12, 12, 14}, {110, 150, 255}, {0, 170, 0}, {0, 170, 170},
	{170, 0, 0}, {170, 0, 170}, {200, 140, 40}, {180, 180, 180},
	{90, 90, 95}, {160, 190, 255}, {85, 255, 85}, {85, 255, 255},
	{230, 70, 70}, {255, 85, 255}, {255, 200, 80}, {220, 220, 220}
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

	(void)w;
	(void)h;
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
	const int margin = CONSOLE_FB_MARGIN_PX;
	int usable_w;
	int usable_h;

	fb_scale = 1;
	fb_cell_w = FONT_PRODUCT_WIDTH;
	fb_cell_h = FONT_PRODUCT_HEIGHT;

	usable_w = (int)w - 2 * margin;
	usable_h = (int)h - 2 * margin;
	if (usable_w < fb_cell_w)
		usable_w = (int)w;
	if (usable_h < fb_cell_h)
		usable_h = (int)h;

	fb_console_cols = usable_w / fb_cell_w;
	fb_console_rows = usable_h / fb_cell_h;
	if (fb_console_cols < 40)
		fb_console_cols = 40;
	if (fb_console_rows < 12)
		fb_console_rows = 12;
	if (fb_console_cols > CONSOLE_MAX_WIDTH)
		fb_console_cols = CONSOLE_MAX_WIDTH;
	if (fb_console_rows > CONSOLE_MAX_HEIGHT)
		fb_console_rows = CONSOLE_MAX_HEIGHT;

	/* Fit again if clamped geometry exceeds the panel. */
	while (fb_console_cols * fb_cell_w > (int)w && fb_console_cols > 40)
		fb_console_cols--;
	while (fb_console_rows * fb_cell_h > (int)h && fb_console_rows > 12)
		fb_console_rows--;

	pw = fb_console_cols * fb_cell_w;
	ph = fb_console_rows * fb_cell_h;
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
	int dx;

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
	glyph = font_terminus_14x28[idx];

	/* Native Terminus 14×28: 2 bytes/row, MSB = leftmost pixel. */
	for (dy = 0; dy < FONT_PRODUCT_HEIGHT; dy++)
	{
		const unsigned char *row_bytes = glyph + dy * 2;
		uint32_t *line = (uint32_t *)(fb + (py + dy) * pitch);

		for (dx = 0; dx < FONT_PRODUCT_WIDTH; dx++)
		{
			uint8_t bit = row_bytes[dx >> 3] & (uint8_t)(0x80u >> (dx & 7));
			uint32_t pixel = bit ? fg_rgb : bg_rgb;

			line[px + dx] = pixel;
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

static int fbcon_init(struct console_geometry *geo)
{
	uint32_t w;
	uint32_t h;
	uint32_t bpp;

	/*
	 * vbe_init() may register a 16bpp VGA-text fallback on x86. That must
	 * not enable the framebuffer console path (ARM has no VGA text mode).
	 */
	if (!vbe_is_available() || !vbe_get_info(&w, &h, &bpp) || bpp != 32 ||
	    w < 640 || h < 400)
		return -1;

	fb_compute_layout(w, h);
	geo->pixel_width = w;
	geo->pixel_height = h;
	geo->cell_width = (unsigned)fb_cell_w;
	geo->cell_height = (unsigned)fb_cell_h;
	geo->columns = (unsigned)fb_console_cols;
	geo->rows = (unsigned)fb_console_rows;
	geo->scale = (unsigned)fb_scale;
	return 0;
}

const struct console_video_ops fbcon_video_ops = {
	.is_framebuffer = 1,
	.init = fbcon_init,
	.put_cell = put_cell_fb,
	.scroll_up = scroll_up_fb,
	.clear = clear_fb,
};
#endif

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
