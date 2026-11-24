/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mm.c
 * Description: ARM64 address-space policy for the common MM.
 *
 * User roots are 4-level 48-bit TTBR0 tables built and walked by the common
 * mm/paging/paging.c code through the descriptor encoders in mm_ops.c, exactly as on
 * x86-64. Each root carries a supervisor identity map (DRAM window and Device
 * blocks, see mmu.c) so exception entry runs with the process root loaded.
 * There is no TTBR1 higher-half kernel yet.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <asm/boot.h>
#include <asm/pgtable.h>
#include <ir0/errno.h>
#include <ir0/mm_range.h>
#include <ir0/page_table_pool.h>
#include <ir0/page.h>
#include <ir0/pmm.h>
#include <ir0/paging.h>
#include <ir0/video_backend.h>
#include <string.h>

#include "mmu.h"

static int mm_map_platform_supervisor(address_space_root_t root);

uintptr_t mm_kernel_heap_start(void)
{
	/* Inside the KIMAGE_VADDR mapping, after the image (asm/boot.h). */
	return (uintptr_t)KIMAGE_VADDR + KERNEL_HEAP_OFFSET;
}

size_t mm_kernel_heap_size(void)
{
	return KIMAGE_MAP_SIZE - KERNEL_HEAP_OFFSET;
}

uintptr_t mm_user_va_start(void)
{
	return 0x00400000UL;
}

uintptr_t mm_user_va_end(void)
{
	/* 48-bit TTBR0 (TCR_EL1.T0SZ = 16). */
	return 0x0000FFFFFFFFFFFFUL;
}

int mm_large_identity_supported(void)
{
	/* L2 block descriptors map the DRAM window and Device MMIO. */
	return 1;
}

/* 4 KiB granule: level-1 block descriptors map 1 GiB. */
int mm_pud_leaf_supported(void)
{
	return 1;
}

unsigned mm_user_root_slots(void)
{
	/* TTBR0 covers only the low half: every L0 slot belongs to the root. */
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
	/* No shared kernel half: the supervisor identity is per root (mmu.c). */
}

/*
 * Every root identity-maps all of DRAM (kernel image, heap, page tables and
 * other PMM frames) and the Device blocks: a user page over any of them
 * would hide kernel memory from the kernel itself while that root runs.
 */
int mm_user_range_mappable(uintptr_t addr, size_t size)
{
	if (!mm_user_range_ok(addr, size, mm_user_va_start(),
			      mm_user_va_end()))
		return 0;
	return !arm64_mmu_supervisor_overlaps(addr, size);
}

address_space_root_t mm_create_user_root(void)
{
	address_space_root_t root;
	uintptr_t root_phys;

	root_phys = page_table_frame_alloc(MM_ALLOC_DIRECT_RECLAIM);
	if (!root_phys)
	{
		paging_oom_note("mm_create_user_root", paging_oom_classify_current());
		return NULL;
	}
	root = phys_to_virt(root_phys);
	memset(root, 0, PAGE_SIZE_4KB);

	if (arm64_mmu_map_supervisor(root) != 0 ||
	    mm_map_platform_supervisor(root) != 0)
	{
		paging_reclaim_user_tables(root);
		page_table_frame_free(root_phys);
		return NULL;
	}

	paging_ir0_mm_note_root_created(root_phys);
	return root;
}

void mm_destroy_user_root(address_space_root_t root)
{
	if (!root)
		return;
	paging_reclaim_user_tables(root);
	paging_ir0_mm_note_root_freed(virt_to_phys(root));
	page_table_frame_free(virt_to_phys(root));
}

void mm_prepare_root_activation(address_space_root_t root)
{
	if (root && virt_to_phys(root) != arm64_mmu_kernel_root())
		(void)arm64_mmu_sync_supervisor(root);
}

/*
 * MMIO device blocks are part of arm64_mmu_map_supervisor(); the
 * framebuffer needs its own write-combining pages, which a cacheable
 * DRAM block of the supervisor identity map would otherwise cover.
 */
static int mm_map_platform_supervisor(address_space_root_t root)
{
	if (video_backend_map_supervisor(root) != 0)
		return -ENOMEM;
	return 0;
}

