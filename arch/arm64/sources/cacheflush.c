/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cacheflush.c
 * Description: ARM64 instruction-cache maintenance (Linux
 *              caches_clean_inval_pou() / icache_inval_all_pou()).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/cacheflush.h>

/* CTR_EL0 fields (Arm ARM D19.2.34). */
#define CTR_IMINLINE(ctr) ((ctr) & 0xFUL)
#define CTR_L1IP(ctr)     (((ctr) >> 14) & 0x3UL)
#define CTR_DMINLINE(ctr) (((ctr) >> 16) & 0xFUL)
#define CTR_IDC           (1UL << 28)
#define CTR_DIC           (1UL << 29)
#define CTR_L1IP_PIPT     0x3UL

void flush_icache_range(uintptr_t start, uintptr_t end)
{
	uint64_t ctr;
	uintptr_t line;
	uintptr_t addr;

	if (start >= end)
		return;
	__asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));

	/* Clean the new instructions to the point of unification. */
	if (!(ctr & CTR_IDC))
	{
		line = 4UL << CTR_DMINLINE(ctr);
		for (addr = start & ~(line - 1); addr < end; addr += line)
			__asm__ volatile("dc cvau, %0" : : "r"(addr) : "memory");
	}
	__asm__ volatile("dsb ish" ::: "memory");

	if (!(ctr & CTR_DIC))
	{
		/*
		 * A VIPT instruction cache may hold the user alias, which a
		 * by-VA invalidate through the kernel alias does not reach.
		 */
		if (CTR_L1IP(ctr) != CTR_L1IP_PIPT)
		{
			__asm__ volatile("ic ialluis" ::: "memory");
		}
		else
		{
			line = 4UL << CTR_IMINLINE(ctr);
			for (addr = start & ~(line - 1); addr < end; addr += line)
				__asm__ volatile("ic ivau, %0"
						 : : "r"(addr) : "memory");
		}
		__asm__ volatile("dsb ish" ::: "memory");
	}
	__asm__ volatile("isb" ::: "memory");
}
