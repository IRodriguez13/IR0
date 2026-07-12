/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: pl011.h
 * Description: QEMU virt PL011 UART0 MMIO helpers (0x09000000).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

void pl011_init(void);
void pl011_putc(char c);
void pl011_puts(const char *s);
