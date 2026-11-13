/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: page_table_pool.c
 * Description: Reclaimable PMM-backed translation-table frame pool.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/atomic.h>
#include <ir0/cpu.h>
#include <ir0/page_table_pool.h>
#include <ir0/pmm.h>
#include <ir0/reclaim.h>

#define PAGE_TABLE_CACHE_FRAMES 16U

static uintptr_t cached_frames[PAGE_TABLE_CACHE_FRAMES];
static size_t cached_count;
static atomic_t reclaimer_state;

static uint64_t page_table_cache_count(void *context)
{
	unsigned long flags;
	uint64_t count;

	(void)context;
	flags = irq_save();
	count = cached_count;
	irq_restore(flags);
	return count;
}

static int page_table_cache_scan(void *context, uint64_t target,
				 uint64_t *reclaimed)
{
	uint64_t released = 0;

	(void)context;
	if (!reclaimed)
		return -1;
	while (released < target)
	{
		unsigned long flags;
		uintptr_t frame;

		flags = irq_save();
		if (cached_count == 0)
		{
			irq_restore(flags);
			break;
		}
		frame = cached_frames[--cached_count];
		irq_restore(flags);
		pmm_free_frame(frame);
		released++;
	}
	*reclaimed = released;
	return 0;
}

static const struct ir0_reclaimer page_table_reclaimer = {
	.name = "page-table-cache",
	.count = page_table_cache_count,
	.scan = page_table_cache_scan,
	.ctx = NULL,
};

static void page_table_pool_register_reclaimer(void)
{
	if (atomic_read_acquire(&reclaimer_state) == 2)
		return;
	if (atomic_cmpxchg(&reclaimer_state, 0, 1) != 0)
		return;
	if (mm_reclaimer_register(&page_table_reclaimer) == 0)
		atomic_set_release(&reclaimer_state, 2);
	else
		atomic_set_release(&reclaimer_state, 0);
}

uintptr_t page_table_frame_alloc(mm_alloc_context_t context)
{
	unsigned long flags;
	uintptr_t frame = 0;

	page_table_pool_register_reclaimer();
	flags = irq_save();
	if (cached_count != 0)
		frame = cached_frames[--cached_count];
	irq_restore(flags);
	if (frame == 0)
		frame = mm_alloc_frame(context);
	if (frame != 0)
		pmm_frame_set_owner_class(frame, PMM_OWNER_PAGE_TABLE, -1);
	return frame;
}

void page_table_frame_free(uintptr_t frame)
{
	unsigned long flags;

	if (frame == 0)
		return;
	page_table_pool_register_reclaimer();
	flags = irq_save();
	if (cached_count < PAGE_TABLE_CACHE_FRAMES)
	{
		cached_frames[cached_count++] = frame;
		irq_restore(flags);
		return;
	}
	irq_restore(flags);
	pmm_free_frame(frame);
}
