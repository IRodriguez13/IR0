/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdint.h>

/*
 * Base of the ARM64 linear map, as in Linux with 48-bit VAs: the bottom of
 * the TTBR1 (kernel) half.
 */
#define IR0_ARCH_PAGE_OFFSET 0xffff000000000000UL

/* Until the image moves to the kernel half it is identity mapped low. */
static inline uintptr_t __virt_to_phys(uintptr_t va)
{
	if ((intptr_t)va < 0)
		return va - IR0_ARCH_PAGE_OFFSET;
	return va;
}
