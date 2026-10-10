/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: mm_range.h
 * Description: ISA-neutral virtual-address range validation helpers.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Validate the half-open range [address, address + size) against the
 * architecture-supplied user window [lower, upper).  Keep the arithmetic
 * here so every ISA gets the same overflow behaviour; each backend owns only
 * its address-space limits.
 */
static inline int mm_user_range_ok(uintptr_t address, size_t size,
				   uintptr_t lower, uintptr_t upper)
{
	uintptr_t end;

	if (address == 0)
		return 0;
	end = address + size;
	if (end < address)
		return 0;
	if (address < lower || end > upper)
		return 0;
	return 1;
}
