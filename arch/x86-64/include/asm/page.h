/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdint.h>

/*
 * Base of the x86-64 direct map, as in Linux with 4-level paging: PML4 slot
 * 273 of the kernel half, shared by every process root.
 */
#define IR0_ARCH_PAGE_OFFSET 0xffff888000000000UL

/*
 * The kernel image and heap are identity mapped low; everything in the
 * kernel half is the direct map.
 */
static inline uintptr_t __virt_to_phys(uintptr_t va)
{
	if ((intptr_t)va < 0)
		return va - IR0_ARCH_PAGE_OFFSET;
	return va;
}
