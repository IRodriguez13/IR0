/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: keyboard.h
 * Description: Portable keyboard backend contract. x86-64 implements it over PS/2;
 *              ISAs without a PS/2 keyboard use the no-op backend in
 *              drivers/input/input_backend.c.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

char keyboard_buffer_get(void);
int keyboard_buffer_has_data(void);
void keyboard_buffer_clear(void);
void keyboard_all_keys_up(void);
int keyboard_set_layout(int layout);
int keyboard_get_layout(void);
const char *keyboard_get_layout_name(int layout);
int keyboard_set_console_mode(int mode);
int keyboard_get_console_mode(void);
