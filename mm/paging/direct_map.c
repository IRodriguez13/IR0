/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: direct_map.c
 * Description: Boot-time direct map of all RAM at PAGE_OFFSET (Linux
 *              init_mem_mapping / map_mem). Built once, never changed.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/paging.h>
#include <ir0/boot_memory.h>
#include <ir0/direct_map.h>
#include <ir0/errno.h>
#include <ir0/page.h>
#include <string.h>

#define DIRECT_MAP_1G (512ULL * PAGE_SIZE_2MB)

/* Kernel-only, never executable, global: the map is the same in every mm. */
#define DIRECT_MAP_FLAGS (PAGE_PRESENT | PAGE_RW | IR0_MM_MAP_GLOBAL)

/*
 * The tables come from boot_memory_early_page() inside the boot identity
 * map. They are written through that identity because the direct map that
 * would reach them is what this file is building.
 */
static uint64_t *early_table(uintptr_t phys)
{
	return (uint64_t *)phys;
}

static uint64_t *next_table(uint64_t *table, size_t index)
{
	uintptr_t phys;
	uint64_t *next;

	if (mm_pte_present(table[index]))
	{
		if (mm_pte_large(table[index]))
			return NULL;
		return early_table(mm_pte_phys(table[index]));
	}
	phys = boot_memory_early_page();
	if (!phys)
		return NULL;
	next = early_table(phys);
	memset(next, 0, PAGE_SIZE_4KB);
	table[index] = mm_make_table_pte(phys, 0);
	return next;
}

/* Map [phys, end) at PAGE_OFFSET + phys with the largest leaves that fit. */
static int direct_map_range(uint64_t *root, uint64_t phys, uint64_t end,
			    int gbpages)
{
	while (phys < end)
	{
		uintptr_t va = (uintptr_t)phys_to_virt((uintptr_t)phys);
		uint64_t left = end - phys;
		size_t idx[4];
		uint64_t *l1;
		uint64_t *l2;
		uint64_t *l3;

		mm_va_indices(va, idx);
		l1 = next_table(root, idx[0]);
		if (!l1)
			return -ENOMEM;
		if (gbpages && (phys & (DIRECT_MAP_1G - 1U)) == 0 &&
		    left >= DIRECT_MAP_1G && !mm_pte_present(l1[idx[1]]))
		{
			l1[idx[1]] = mm_make_leaf_pte((uintptr_t)phys,
						      DIRECT_MAP_FLAGS |
						      PAGE_SIZE_2MB_FLAG, 0);
			phys += DIRECT_MAP_1G;
			continue;
		}
		l2 = next_table(l1, idx[1]);
		if (!l2)
			return -ENOMEM;
		if ((phys & (PAGE_SIZE_2MB - 1U)) == 0 && left >= PAGE_SIZE_2MB &&
		    !mm_pte_present(l2[idx[2]]))
		{
			l2[idx[2]] = mm_make_leaf_pte((uintptr_t)phys,
						      DIRECT_MAP_FLAGS |
						      PAGE_SIZE_2MB_FLAG, 0);
			phys += PAGE_SIZE_2MB;
			continue;
		}
		l3 = next_table(l2, idx[2]);
		if (!l3)
			return -ENOMEM;
		l3[idx[3]] = mm_make_leaf_pte((uintptr_t)phys, DIRECT_MAP_FLAGS, 0);
		phys += PAGE_SIZE_4KB;
	}
	return 0;
}

int direct_map_init(void)
{
	struct ir0_phys_range ranges[IR0_BOOT_MEMORY_MAX_BANKS];
	uint64_t *root = mm_kernel_map_root();
	int gbpages = mm_pud_leaf_supported();
	size_t count;
	size_t i;
	int ret;

	if (!root)
		return -ENODEV;
	ret = boot_memory_get_ram_ranges(ranges, IR0_BOOT_MEMORY_MAX_BANKS,
					 &count);
	if (ret != 0)
		return ret;
	for (i = 0; i < count; i++)
	{
		ret = direct_map_range(root, ranges[i].base,
				       ranges[i].base + ranges[i].size, gbpages);
		if (ret != 0)
			return ret;
	}
	mm_kernel_map_enable();
	return 0;
}
