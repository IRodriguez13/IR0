/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: pl011.h
 * Description: PL011 UART helpers — MMIO base from arm64_board (virt / RPi4).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

void pl011_init(void);
void pl011_init_at(uintptr_t base);
void pl011_putc(char c);
void pl011_puts(const char *s);
