/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: typewriter.h
 * Description: Kernel typewriter console effect facade
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

void typewriter_init(void);
void typewriter_vga_print(const char *str, uint8_t color);
void typewriter_vga_print_char(char c, uint8_t color);
/* Kernel text (print()): drawn with @color, recorded in the scrollback. */
void typewriter_putchar_attr(char c, uint8_t color);
void typewriter_show_cursor(uint8_t color);
int typewriter_cursor_x(void);
void typewriter_console_scroll(int delta);
void typewriter_console_clear(uint8_t color);
