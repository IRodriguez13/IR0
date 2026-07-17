/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: klog.c
 * Description: klog_* → serial_* adapter (behavior unchanged).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/klog.h>
#include <ir0/serial_io.h>

void klog_print(const char *str)
{
	serial_print(str);
}

void klog_hex32(uint32_t num)
{
	serial_print_hex32(num);
}

void klog_hex64(uint64_t num)
{
	serial_print_hex64(num);
}
