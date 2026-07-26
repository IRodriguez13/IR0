/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: keyboard.h
 * Description: PS/2 keyboard IRQ API; modifiers via <ir0/ps2_set1.h>.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

#define KEYBOARD_LAYOUT_US     0
#define KEYBOARD_LAYOUT_LATAM  1

char keyboard_buffer_get(void);
int keyboard_buffer_has_data(void);
void keyboard_buffer_clear(void);

void keyboard_handler64(void);
void keyboard_poll_ps2(void);

void keyboard_init(void);

int keyboard_set_layout(int layout);
int keyboard_get_layout(void);
const char *keyboard_get_layout_name(int layout);

/* Momentary + lock modifier snapshot (see ps2_set1.h masks). */
uint32_t keyboard_modifiers_mask(void);
int keyboard_ctrl_active(void);
/*
 * Resync momentary keys only (device reinit / overflow / focus loss).
 * Does not replace correct break-code processing.
 */
void keyboard_all_keys_up(void);

void set_idle_mode(int is_idle);
int is_in_idle_mode(void);
void wakeup_from_idle(void);
