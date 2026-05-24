/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 unified text console renderer (Minix-style single cursor state).
 */

#pragma once

#include <stdint.h>

#define CONSOLE_RENDERER_COLOR_DEFAULT 0x07u /* light grey on black */

void console_renderer_reset(uint8_t color);
void console_renderer_putchar(char c, uint8_t color);
void console_renderer_show_cursor(uint8_t color);
int console_renderer_get_cursor_x(void);
int console_renderer_get_cursor_y(void);
void console_renderer_import_cursor_pos(int cols);
