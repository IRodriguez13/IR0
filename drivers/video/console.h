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
#include <ir0/video_console.h>

/* FB shadow / soft cursor bounds (Terminus 14×28 ≈ 88×27 on 1280×800). */
#define CONSOLE_MAX_WIDTH  128
#define CONSOLE_MAX_HEIGHT 48

#define CONSOLE_FB_MARGIN_PX     20

/*
 * Video backend of the console: draws, scrolls and clears cells. The console
 * core owns the cell shadow and the geometry and calls only these hooks.
 * init() returns 0 when the backend can drive the screen and fills @geo;
 * a backend without init() is always usable with the default text geometry.
 */
struct console_video_ops
{
	int is_framebuffer;
	int (*init)(struct console_geometry *geo);
	void (*put_cell)(int row, int col, char c, uint8_t color);
	void (*scroll_up)(uint8_t clear_color);
	void (*clear)(uint8_t color);
};

extern const struct console_video_ops textcon_video_ops;
extern const struct console_video_ops fbcon_video_ops;

/*
 * console_draw_cell - Paint without updating the cell shadow (software cursor).
 */
void console_draw_cell(int row, int col, char c, uint8_t color);

/*
 * console_get_cell - Return shadowed (char | color<<8) for software cursor restore.
 */
uint16_t console_get_cell(int row, int col);

#endif /* CONSOLE_H */
