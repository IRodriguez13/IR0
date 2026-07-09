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

#define TYPEWRITER_DELAY_FAST    2000
#define TYPEWRITER_DELAY_NORMAL  5000
#define TYPEWRITER_DELAY_SLOW    8000

typedef enum
{
	TYPEWRITER_DISABLED = 0,
	TYPEWRITER_FAST,
	TYPEWRITER_NORMAL,
	TYPEWRITER_SLOW
} typewriter_mode_t;

void typewriter_init(void);
void typewriter_set_mode(typewriter_mode_t mode);
typewriter_mode_t typewriter_get_mode(void);
void typewriter_print(const char *str);
void typewriter_print_char(char c);
void typewriter_print_uint32(uint32_t num);
void typewriter_vga_print(const char *str, uint8_t color);
void typewriter_vga_print_char(char c, uint8_t color);
void typewriter_show_cursor(uint8_t color);
int typewriter_cursor_x(void);
void typewriter_console_scroll(int delta);
void typewriter_console_clear(uint8_t color);
void typewriter_enable_for_commands(int enable);
int typewriter_is_enabled_for_commands(void);
