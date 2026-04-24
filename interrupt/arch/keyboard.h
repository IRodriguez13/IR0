/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: keyboard.h
 * Description: IR0 kernel source/header file
 */

#pragma once

#include <stdint.h>

/*
 * Ring buffer sizing lives in keyboard.c (KERNEL_KBD_RING_SIZE).
 * Userspace/shared layout: KEYBOARD_BUFFER_ADDR / KEYBOARD_BUFFER_SIZE in config.h.
 */

#define KEYBOARD_LAYOUT_US     0
#define KEYBOARD_LAYOUT_LATAM  1

char keyboard_buffer_get(void);
int keyboard_buffer_has_data(void);
void keyboard_buffer_clear(void);

void keyboard_handler64(void);

void keyboard_init(void);

char translate_scancode(uint8_t sc);
int keyboard_set_layout(int layout);
int keyboard_get_layout(void);
const char *keyboard_get_layout_name(int layout);

void set_idle_mode(int is_idle);
int is_in_idle_mode(void);
void wakeup_from_idle(void);
