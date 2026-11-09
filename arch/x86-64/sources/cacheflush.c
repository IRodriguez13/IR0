/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cacheflush.c
 * Description: x86-64 instruction-cache maintenance: coherent, nothing to do.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/cacheflush.h>

void flush_icache_range(uintptr_t start, uintptr_t end)
{
	(void)start;
	(void)end;
}
