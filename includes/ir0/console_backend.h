/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Console backend facade
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <drivers/video/console.h>
#include <drivers/video/typewriter.h>

#define IR0_CONSOLE_COLOR_DEFAULT 0x07u /* light grey on black */

void console_backend_init(void);
void console_backend_clear(uint8_t color);
int console_backend_uses_framebuffer(void);
int console_backend_fb_scale(void);
void console_backend_scroll(int lines);
void console_backend_write(const char *str, size_t len, uint8_t color);
void console_backend_show_cursor(uint8_t color);
int console_backend_cursor_x(void);
void console_backend_userspace_handoff(void);
int console_backend_printk_to_screen(void);
void console_backend_typewriter_init(void);
