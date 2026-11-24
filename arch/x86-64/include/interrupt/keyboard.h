/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: keyboard.h
 * Description: i8042 keyboard: IRQ setup, controller drain, device init.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

void i8042_irq_init(void);
void keyboard_poll_ps2(void);

void keyboard_init(void);
/* Hand every byte from i8042 port 2 (AUX) to @feed. */
void i8042_register_aux(void (*feed)(uint8_t data));

void set_idle_mode(int is_idle);
int is_in_idle_mode(void);
void wakeup_from_idle(void);
