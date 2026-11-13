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
#include <ir0/clock.h>
#include <ir0/kmem.h>
#include <ir0/mm_port.h>
#include <ir0/page_table_pool.h>
#include <ir0/pmm.h>
#include <ir0/reclaim.h>
#include <mm/alloc/allocator.h>
#include <stdint.h>
#include <string.h>

/* Bound for one calibration wait: far beyond any sane tick period. */
#define HEAP_TICK_SPIN_LIMIT 400000000UL
#define HEAP_LOCKED_TICKS 4

static int heap_hook_armed;
static unsigned long heap_hook_spins;
static uint64_t heap_hook_ticks;

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

/*
 * Read the tick count until it moves or @limit reads pass; returns the
 * reads spent. The test needs the CPU held on purpose (it simulates a long
 * heap critical section), so this is a bounded loop of clock reads, not a
 * wait. Calibration and the hook use the same loop, so a count measured
 * with IRQs on spans the same wall time inside the hook.
 */
static unsigned long heap_spin_ticks(unsigned long limit)
{
	uint64_t start = clock_get_tick_count();
	unsigned long n;

	for (n = 0; n < limit; n++)
	{
		if (clock_get_tick_count() != start)
			break;
	}
	return n;
}

void heap_locked_test_hook(void)
{
	uint64_t start;
	unsigned long i;

	if (!heap_hook_armed)
		return;
	heap_hook_armed = 0;
	start = clock_get_tick_count();
	for (i = 0; i < HEAP_LOCKED_TICKS; i++)
		(void)heap_spin_ticks(heap_hook_spins);
	heap_hook_ticks = clock_get_tick_count() - start;
}

/*
 * kmalloc/kfree run from IRQ and network paths, so the heap critical
 * section must mask IRQs: a timer IRQ must not be taken inside it. The
 * hook spins about HEAP_LOCKED_TICKS tick periods (calibrated with IRQs
 * on) inside heap_alloc's critical section; the tick count must not move.
 */
void ktest_heap_masks_irqs(void)
{
	void *p;

	KTEST_BEGIN("heap_masks_irqs");

	/* Align to a tick edge, then time one full period with IRQs on. */
	(void)heap_spin_ticks(HEAP_TICK_SPIN_LIMIT);
	heap_hook_spins = heap_spin_ticks(HEAP_TICK_SPIN_LIMIT);
	KASSERT(heap_hook_spins > 0);
	KASSERT(heap_hook_spins < HEAP_TICK_SPIN_LIMIT);

	heap_hook_ticks = 0;
	heap_hook_armed = 1;
	p = kmalloc(32);
	heap_hook_armed = 0;
	KASSERT(p != NULL);
	kfree(p);
	KASSERT_EQ(heap_hook_ticks, 0);

	KTEST_END();
}

/*
 * kfree must recognise what is not a live kmalloc block: a kmalloc_aligned
 * pointer, an interior pointer, and a stale pointer whose old header a
 * coalesce absorbed and a later allocation zeroed (is_free reads 0 there,
 * so the plain double-free check passes it).
 */
void ktest_heap_block_check(void)
{
	uint8_t *interior;
	void *aligned;
	void *a;
	void *b;
	void *c;
	void *guard;

	KTEST_BEGIN("heap_block_check");

	a = kmalloc(64);
	b = kmalloc(64);
	guard = kmalloc(64);
	KASSERT(a != NULL);
	KASSERT(b != NULL);
	KASSERT(guard != NULL);
	KASSERT_EQ(heap_block_check(a), HEAP_BLOCK_OK);
	KASSERT_EQ(heap_block_check(b), HEAP_BLOCK_OK);

	/* b coalesces into a; the merged block heads the free list. */
	kfree(a);
	KASSERT_EQ(heap_block_check(a), HEAP_BLOCK_FREE);
	kfree(b);
	c = kmalloc(150);
	KASSERT(c == a);
	KASSERT_EQ(heap_block_check(c), HEAP_BLOCK_OK);
	KASSERT_EQ(heap_block_check(b), HEAP_BLOCK_BAD);
	kfree(c);
	kfree(guard);

	interior = kmalloc(256);
	KASSERT(interior != NULL);
	KASSERT_EQ(heap_block_check(interior + 64), HEAP_BLOCK_BAD);
	kfree(interior);

	aligned = kmalloc_aligned(64, 64);
	KASSERT(aligned != NULL);
	KASSERT_EQ(heap_block_check(aligned), HEAP_BLOCK_BAD);
	kfree_aligned(aligned);

	KTEST_END();
}
