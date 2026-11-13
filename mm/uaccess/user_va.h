/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: user_va.h
 * Description: ISA user-window check shared by the mm/ units. Not public.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <ir0/arch_mm.h>
#include <ir0/mm_range.h>

/*
 * mm_user_va_ok - Nonzero when [@addr, @addr + @size) lies in the ISA user
 * window (no page walk). The arithmetic behind access_ok() and the user
 * copies; outside mm a pointer is checked with access_ok() and a mapping
 * range with mm_user_range_mappable() (arch-guard [uaccess-single-api]).
 */
static inline int mm_user_va_ok(uintptr_t addr, size_t size)
{
	return mm_user_range_ok(addr, size, mm_user_va_start(),
				mm_user_va_end());
}
