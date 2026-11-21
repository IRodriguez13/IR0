/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 unified text console renderer (Minix-style single cursor state).
 */

#pragma once

#include <stdint.h>

#define CONSOLE_RENDERER_COLOR_DEFAULT 0x07u /* light grey on black */

/* Linear cursor (row * cols + col) shared by the renderer and the scrollback. */
extern int cursor_pos;

void console_renderer_reset(uint8_t color);
void console_renderer_putchar(char c, uint8_t color);
void console_renderer_putchar_attr(char c, uint8_t attr);
void console_renderer_show_cursor(uint8_t color);
int console_renderer_get_cursor_x(void);
