/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_mm.c
 * Description: ARM64 root-table helpers (TTBR0-only identity model).
 *
 * IR0 arm64 uses a single TTBR0 root (mmu_early / mm_activate) — no TTBR1
 * high-half kernel yet. Kernel/shared identity is installed by
 * process_mm_create_root() via map_supervisor_identity_low(), not by
 * copying a PML4-style kernel half. COW/unmap walks the full 512-slot root.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_mm.h>
#include <ir0/mm_range.h>
#include <ir0/oops.h>
#include <ir0/errno.h>
#include <ir0/pmm.h>
#include <ir0/arm64_board.h>
#include "mmu_early.h"

uintptr_t mm_kernel_heap_start(void)
{
	/* QEMU virt DRAM starts at 0x40000000; leave 8 MiB for the image. */
	return 0x40800000UL;
}

size_t mm_kernel_heap_size(void)
{
	return 0x01800000UL;
}

int mm_boot_pmm_range(uintptr_t *base, size_t *size)
{
	const struct arm64_board_boot_info *boot = arm64_board_boot_info();
	uint64_t best_base = 0;
	uint64_t best_size = 0;
	uint32_t i;

	if (!base || !size)
		return -EINVAL;
	if (!boot || !boot->fdt_valid)
		return -ENODEV;
	for (i = 0; i < boot->usable_range_count; i++)
	{
		uint64_t range_base = boot->usable[i].base;
		uint64_t range_end = range_base + boot->usable[i].size;
		uint64_t candidate_base;
		uint64_t candidate_size;

		if (range_end < range_base || boot->usable[i].size < 0x200000U)
			continue;
		/* Use the upper half: low RAM contains image, DTB and early EL0 data. */
		candidate_base = (range_base + (boot->usable[i].size / 2U) +
				  0xFFFU) & ~0xFFFULL;
		if (candidate_base >= range_end)
			continue;
		candidate_size = (range_end - candidate_base) & ~0xFFFULL;
		if (candidate_size > best_size)
		{
			best_base = candidate_base;
			best_size = candidate_size;
		}
	}
	if (best_size < 0x200000U)
		return -EINVAL;
	*base = (uintptr_t)best_base;
	*size = (size_t)best_size;
	return 0;
}

size_t mm_boot_usable_range_count(void)
{
	const struct arm64_board_boot_info *boot = arm64_board_boot_info();

	if (!boot || !boot->fdt_valid)
		return 0;
	return boot->usable_range_count;
}

int mm_boot_usable_range(size_t index, struct ir0_phys_range *range)
{
	const struct arm64_board_boot_info *boot = arm64_board_boot_info();

	if (!range)
		return -EINVAL;
	if (!boot || !boot->fdt_valid)
		return -ENODEV;
	if (index >= boot->usable_range_count)
		return -EINVAL;
	*range = boot->usable[index];
	return 0;
}

uintptr_t mm_user_va_start(void)
{
	return 0x00400000UL;
}

uintptr_t mm_user_va_end(void)
{
	/* Current 48-bit TTBR0 policy; the backend owns future VA-size changes. */
	return 0x0000FFFFFFFFFFFFUL;
}

int mm_large_identity_supported(void)
{
	return 0;
}

unsigned mm_user_root_slots(void)
{
	/* Entire TTBR0 L0 is process-owned for walk/COW (no TTBR1 split). */
	return 512;
}

unsigned mm_root_slots(void)
{
	return 512;
}

void mm_copy_kernel_half(uint64_t *dst_root, const uint64_t *src_root)
{
	(void)dst_root;
	(void)src_root;
	/*
	 * No root-slot kernel half: TTBR1 not wired. Caller maps supervisor
	 * identity separately (see process_mm_create_root()).
	 */
}

int mm_user_va_ok(uintptr_t addr, size_t size)
{
	/*
	 * Portable mm path: same canonical low-half window as x86-64 until
	 * TTBR0/TTBR1 split lands. Early EL0 probes still use
	 * arm64_mmu_user_buf_ok behind their own wrappers.
	 */
	return mm_user_range_ok(addr, size, mm_user_va_start(),
				mm_user_va_end());
}

address_space_root_t mm_create_user_root(void)
{
	return (address_space_root_t)(uintptr_t)arm64_mmu_root_alloc_clean();
}

void mm_destroy_user_root(address_space_root_t root)
{
	if (root && arm64_mmu_root_release((uint64_t)(uintptr_t)root) != 0)
		panic("arm64: invalid private root release");
}

void mm_prepare_root_activation(address_space_root_t root)
{
	(void)root;
	/* The staged TTBR0 maps are self-contained until root pooling lands. */
}

int mm_backend_map_user_page(address_space_root_t root, uintptr_t va,
			     uintptr_t pa, uint64_t flags)
{
	if (!root || !(flags & IR0_MM_MAP_USER) ||
	    !arm64_mmu_root_owned((uint64_t)(uintptr_t)root))
		return -EOPNOTSUPP;
	return arm64_mmu_map_user_frame_in_root(
		(uint64_t)(uintptr_t)root, va, pa,
		!!(flags & IR0_MM_MAP_WRITE), !!(flags & IR0_MM_MAP_EXEC));
}

int mm_backend_reclaim_user_frames(address_space_root_t root,
				   uint64_t *reclaimed_pages)
{
	uint64_t reclaimed = 0;
	uint64_t phys;
	int ret;

	if (!root || !arm64_mmu_root_owned((uint64_t)(uintptr_t)root))
		return -EOPNOTSUPP;
	while ((ret = arm64_mmu_take_owned_user_frame(
		(uint64_t)(uintptr_t)root, &phys)) == 0)
	{
		pmm_free_frame((uintptr_t)phys);
		reclaimed++;
	}
	if (ret != -EINVAL)
		return ret;
	if (reclaimed_pages)
		*reclaimed_pages = reclaimed;
	return 0;
}

int mm_backend_unmap_user_page(address_space_root_t root, uintptr_t va)
{
	if (!root || !arm64_mmu_root_owned((uint64_t)(uintptr_t)root))
		return -EOPNOTSUPP;
	return arm64_mmu_unmap_user_page_in_root((uint64_t)(uintptr_t)root, va);
}

int mm_backend_lookup_user_page(address_space_root_t root, uintptr_t va,
				uintptr_t *phys, uint64_t *flags)
{
	uint64_t arm64_phys;
	int ret;

	if (!root || !arm64_mmu_root_owned((uint64_t)(uintptr_t)root))
		return -EOPNOTSUPP;
	ret = arm64_mmu_user_page_info_in_root((uint64_t)(uintptr_t)root, va,
						   &arm64_phys, flags);
	if (ret != 0)
		return ret;
	if (!phys)
		return -EINVAL;
	*phys = (uintptr_t)arm64_phys;
	return 0;
}

int mm_backend_protect_user_range(address_space_root_t root, uintptr_t start,
				  size_t length, uint64_t flags)
{
	if (!root || !(flags & IR0_MM_MAP_USER) ||
	    !arm64_mmu_root_owned((uint64_t)(uintptr_t)root))
		return -EOPNOTSUPP;
	return arm64_mmu_protect_user_range_in_root(
		(uint64_t)(uintptr_t)root, start, length,
		!!(flags & IR0_MM_MAP_WRITE), !!(flags & IR0_MM_MAP_EXEC));
}
