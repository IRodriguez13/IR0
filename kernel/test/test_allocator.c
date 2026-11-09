/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_allocator.c
 * Description: Boot-time kernel heap tests (kmalloc/kcalloc/kfree), run
 *              after heap_init() without a process context.
 */

#include "test/ktest_harness.h"
#include <ir0/boot_memory.h>
#include <ir0/kmem.h>
#include <ir0/mm_port.h>
#include <ir0/page_table_pool.h>
#include <ir0/pmm.h>
#include <ir0/reclaim.h>
#include <mm/allocator.h>
#include <stdint.h>
#include <string.h>

void ktest_allocator(void)
{
	KTEST_BEGIN("allocator");

	void *p = kmalloc(64);
	KASSERT(p != NULL);
	memset(p, 0xAB, 64);
	kfree(p);

	void *q = kmalloc(128);
	KASSERT(q != NULL);
	kfree(q);

	{
		size_t backward_before;
		size_t backward_after;
		void *left;
		void *right;
		void *guard;

		heap_coalesce_stats(NULL, &backward_before);
		left = kmalloc(256);
		right = kmalloc(256);
		guard = kmalloc(64);
		KASSERT(left != NULL);
		KASSERT(right != NULL);
		KASSERT(guard != NULL);
		kfree(left);
		kfree(right);
		heap_coalesce_stats(NULL, &backward_after);
		KASSERT_EQ(backward_after, backward_before + 1);
		kfree(guard);
	}

	{
		uint32_t *arr = kcalloc(16, sizeof(*arr));
		size_t i;

		KASSERT(arr != NULL);
		if (arr)
		{
			for (i = 0; i < 16; i++)
				KASSERT_EQ(arr[i], 0);
			kfree(arr);
		}
		KASSERT(kcalloc(SIZE_MAX / 2 + 1, 2) == NULL);
		KASSERT(kmalloc(0) == NULL);
	}

	{
		struct ir0_reclaim_result result;
		uintptr_t frame;

		frame = page_table_frame_alloc(MM_ALLOC_ATOMIC);
		KASSERT(frame != 0);
		KASSERT(!boot_memory_range_is_reserved(frame, PMM_FRAME_SIZE));
		KASSERT_EQ(pmm_frame_owner_class(frame), PMM_OWNER_PAGE_TABLE);
		page_table_frame_free(frame);
		KASSERT_EQ(mm_reclaim_run(1, &result), 0);
		KASSERT_EQ(result.reclaimed_pages, 1);
	}

	KTEST_END();
}

/*
 * The PMM must never own a frame the kernel keeps using: firmware
 * reservations, the DTB, the initrd or the fixed kernel heap window.
 */
void ktest_pmm_skips_reserved_ranges(void)
{
	KTEST_BEGIN("pmm_skips_reserved_ranges");
	KASSERT_EQ(boot_memory_check_pmm(), 0);
	KTEST_END();
}

/*
 * /proc/meminfo and /sys/kernel/mm print the third heap statistic as
 * SlabAllocs: the number of live heap allocations, not a byte count.
 */
void ktest_allocator_stats(void)
{
	size_t used_before;
	size_t used_mid;
	size_t used_after;
	size_t allocs_before;
	size_t allocs_mid;
	size_t allocs_after;
	void *a;
	void *b;
	void *c;

	KTEST_BEGIN("allocator_stats");

	ir0_mm_alloc_stats(NULL, &used_before, &allocs_before);
	a = kmalloc(1000);
	b = kmalloc(1000);
	c = kmalloc(1000);
	KASSERT(a != NULL);
	KASSERT(b != NULL);
	KASSERT(c != NULL);
	ir0_mm_alloc_stats(NULL, &used_mid, &allocs_mid);
	kfree(a);
	kfree(b);
	kfree(c);
	ir0_mm_alloc_stats(NULL, &used_after, &allocs_after);

	KASSERT(used_mid >= used_before + 3 * 1000);
	KASSERT_EQ(used_after, used_before);
	KASSERT_EQ(allocs_mid, allocs_before + 3);
	KASSERT_EQ(allocs_after, allocs_before);

	KTEST_END();
}
