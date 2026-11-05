/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_mock_console.h
 * Description: Test-only console facade mock control surface.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

void ktm_console_mock_reset(void);

/* Configuration knobs used to exercise both facade branches. */
void ktm_console_mock_set_framebuffer(int on);
void ktm_console_mock_set_fb_scale(int scale);
void ktm_console_mock_set_cursor_x(int x);
void ktm_console_mock_set_printk_to_screen(int on);
void ktm_console_mock_set_serial_mirror(int on);

/* Observed state. */
size_t ktm_console_mock_write_bytes(void);
size_t ktm_console_mock_serial_bytes(void);
const char *ktm_console_mock_text(void);
const char *ktm_console_mock_serial_text(void);
int ktm_console_mock_init_calls(void);
int ktm_console_mock_typewriter_init_calls(void);
int ktm_console_mock_clear_calls(void);
int ktm_console_mock_scroll_lines(void);
int ktm_console_mock_cursor_shows(void);
int ktm_console_mock_handoff_calls(void);
int ktm_console_mock_panic_screen_calls(void);
uint8_t ktm_console_mock_last_color(void);
