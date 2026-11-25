/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <asm/boot.h>
#include <stdint.h>

/*
 * Base of the ARM64 linear map, as in Linux with 48-bit VAs: the bottom of
 * the TTBR1 (kernel) half.
 */
#define IR0_ARCH_PAGE_OFFSET 0xffff000000000000UL

/* Kernel image virtual address minus its physical address. */
extern uint64_t kimage_voffset;

/*
 * The kernel image and its heap live at KIMAGE_VADDR in the TTBR1 half;
 * below it is the linear map. Low addresses are the boot identity map.
 */
static inline uintptr_t __virt_to_phys(uintptr_t va)
{
	if (va >= (uintptr_t)KIMAGE_VADDR)
		return va - kimage_voffset;
	if ((intptr_t)va < 0)
		return va - IR0_ARCH_PAGE_OFFSET;
	return va;
}
