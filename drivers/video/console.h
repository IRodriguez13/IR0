/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: console.h
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Console abstraction
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Unified console interface for VGA text and framebuffer backends.
 * Same calling code works in both modes.
 */

#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

/* VGA text mode geometry (and soft defaults). */
#define CONSOLE_WIDTH  80
#define CONSOLE_HEIGHT 25

/* FB shadow / soft cursor bounds (Terminus 14×28 ≈ 88×27 on 1280×800). */
#define CONSOLE_MAX_WIDTH  128
#define CONSOLE_MAX_HEIGHT 48

/*
 * Product FB uses a native bitmap font (no nearest-neighbor upscale).
 * Scale remains 1; cell size comes from FONT_WIDTH/HEIGHT.
 */
#define CONSOLE_FB_SCALE_DEFAULT 1
#define CONSOLE_FB_SCALE_MAX     1
#define CONSOLE_FB_MARGIN_PX     20

struct console_geometry
{
	unsigned pixel_width;
	unsigned pixel_height;
	unsigned cell_width;
	unsigned cell_height;
	unsigned columns;
	unsigned rows;
	unsigned scale;
};

#define CONSOLE_FB_BORDER_COLOR 0x00u /* black letterbox */

/*
 * console_put_cell - Draw one character cell at (row, col).
 * @row: 0..24
 * @col: 0..79
 * @c: ASCII character
 * @color: VGA color byte (fg in low nibble, bg in high nibble; 0x0F = white on black)
 */
void console_put_cell(int row, int col, char c, uint8_t color);

/*
 * console_draw_cell - Paint without updating the cell shadow (software cursor).
 */
void console_draw_cell(int row, int col, char c, uint8_t color);

/*
 * console_get_cell - Return shadowed (char | color<<8) for software cursor restore.
 */
uint16_t console_get_cell(int row, int col);

/*
 * console_scroll_up - Scroll displayed content up by one line.
 * Bottom line is cleared with spaces.
 */
void console_scroll_up(uint8_t clear_color);

/*
 * console_clear - Clear entire screen with spaces.
 */
void console_clear(uint8_t color);

/*
 * console_init - Initialize console backend.
 * Chooses VGA (0xB8000) or framebuffer based on vbe_is_available() and bpp.
 * Call after vbe_init_from_multiboot / vbe_init.
 */
void console_init(void);

/*
 * console_use_framebuffer - True if using framebuffer backend.
 */
int console_use_framebuffer(void);

int console_get_width(void);
int console_get_height(void);
int console_get_fb_scale(void);
void console_get_geometry(struct console_geometry *geo);

#endif /* CONSOLE_H */
