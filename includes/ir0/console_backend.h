/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Console backend facade
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

void console_backend_init(void);
void console_backend_clear(uint8_t color);
int console_backend_uses_framebuffer(void);
void console_backend_scroll(int lines);
void console_backend_write(const char *str, size_t len, uint8_t color);

