/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: pmm.c
 * Description: Physical Memory Manager implementation
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "pmm.h"
#include <ir0/kmem.h>
#include <ir0/serial_io.h>
#include <ir0/process.h>
#include <ir0/debug_runtime.h>
#include <config.h>
#include <stdint.h>

/* INTERNAL STATE                                                            */

static struct
{
    uintptr_t mem_start;      /* Start of physical memory region */
    uintptr_t mem_end;        /* End of physical memory region */
    size_t total_frames;      /* Total number of 4KB frames */
    uint8_t *bitmap;          /* Bitmap: 1 bit per frame (1=used, 0=free) */
    size_t used_frames;       /* Number of allocated frames */
    int initialized;          /* Initialization flag */
} pmm = {0};

/*
 * Next frame index to try on allocation; wraps. Frees before the hint reset
 * the hint to 0 so holes below the cursor are not skipped indefinitely.
 */
static uint32_t pmm_search_hint;
static int32_t *pmm_frame_owner;
static uint16_t *pmm_frame_refs;
static uint64_t pmm_diag_double_free;
static uint32_t pmm_diag_events;

static int pmm_frame_index(uintptr_t phys_addr, size_t *out_index)
{
	if (!pmm.initialized)
		return -1;
	if (phys_addr < pmm.mem_start || phys_addr >= pmm.mem_end)
		return -1;
	phys_addr &= ~(PMM_FRAME_SIZE - 1);
	*out_index = (phys_addr - pmm.mem_start) / PMM_FRAME_SIZE;
	if (*out_index >= pmm.total_frames)
		return -1;
	return 0;
}

/* BITMAP OPERATIONS                                                         */

/* Set bit at frame_index (mark as used) */
static inline void bitmap_set(size_t frame_index)
{
    pmm.bitmap[frame_index / 8] |= (1 << (frame_index % 8));
}

/* Clear bit at frame_index (mark as free) */
static inline void bitmap_clear(size_t frame_index)
{
    pmm.bitmap[frame_index / 8] &= ~(1 << (frame_index % 8));
}

/* Test bit at frame_index (check if used) */
static inline int bitmap_test(size_t frame_index)
{
    return (pmm.bitmap[frame_index / 8] & (1 << (frame_index % 8))) != 0;
}


void pmm_init(uintptr_t mem_start, size_t mem_size)
{
    if (pmm.initialized)
        return;

    /* Align to frame boundaries */
    pmm.mem_start = (mem_start + PMM_FRAME_SIZE - 1) & ~(PMM_FRAME_SIZE - 1);
    pmm.mem_end = (mem_start + mem_size) & ~(PMM_FRAME_SIZE - 1);
    
    /* Calculate total frames */
    pmm.total_frames = (pmm.mem_end - pmm.mem_start) / PMM_FRAME_SIZE;
    
    /* Allocate bitmap (1 bit per frame) */
    size_t bitmap_size = (pmm.total_frames + 7) / 8;
    pmm.bitmap = kmalloc(bitmap_size);
    
    if (!pmm.bitmap)
    {
#if DEBUG_PMM
        serial_print("[PMM] CRITICAL: Failed to allocate bitmap\n");
#endif
        return;
    }
    
    /* Initialize all frames as free (bitmap = 0) */
    for (size_t i = 0; i < bitmap_size; i++)
        pmm.bitmap[i] = 0;
    
    pmm.used_frames = 0;
    pmm.initialized = 1;
    pmm_search_hint = 0;
    pmm_diag_double_free = 0;
    pmm_diag_events = 0;
    pmm_frame_owner = kmalloc(pmm.total_frames * sizeof(int32_t));
    if (pmm_frame_owner)
    {
        for (size_t i = 0; i < pmm.total_frames; i++)
            pmm_frame_owner[i] = -1;
    }
    pmm_frame_refs = kmalloc(pmm.total_frames * sizeof(uint16_t));
    if (pmm_frame_refs)
    {
        for (size_t i = 0; i < pmm.total_frames; i++)
            pmm_frame_refs[i] = 0;
    }

#if DEBUG_PMM
    serial_print("[PMM] Initialized\n");
#endif
}

uintptr_t pmm_alloc_frame(void)
{
    if (!pmm.initialized)
        return 0;
    
    /* First-fit from search hint, then wrap [0, hint) */
    size_t start = (size_t)pmm_search_hint;
    if (start >= pmm.total_frames)
        start = 0;

    for (size_t pass = 0; pass < 2; pass++)
    {
        size_t i = (pass == 0) ? start : 0;
        size_t end = (pass == 0) ? pmm.total_frames : start;

        for (; i < end; i++)
        {
            if (!bitmap_test(i))
            {
                pid_t owner_pid = process_get_pid();
                bitmap_set(i);
                pmm.used_frames++;
                pmm_search_hint = (uint32_t)((i + 1) % pmm.total_frames);
                if (pmm_frame_owner)
                    pmm_frame_owner[i] = (int32_t)owner_pid;
                if (pmm_frame_refs)
                    pmm_frame_refs[i] = 1;
                if (pmm_diag_events < 2048U && IR0_DEBUG_PMM)
                {
                    pmm_diag_events++;
                }

                return pmm.mem_start + (i * PMM_FRAME_SIZE);
            }
        }
    }
    
    /* Out of memory */
#if DEBUG_PMM
    serial_print("[PMM] FAILED: Out of physical memory\n");
#endif
    
    return 0;
}

void pmm_frame_get(uintptr_t phys_addr)
{
	size_t frame_index;

	if (pmm_frame_index(phys_addr, &frame_index) != 0)
		return;
	if (!bitmap_test(frame_index) || !pmm_frame_refs)
		return;
	if (pmm_frame_refs[frame_index] < 0xFFFF)
		pmm_frame_refs[frame_index]++;
}

void pmm_frame_put(uintptr_t phys_addr)
{
	size_t frame_index;

	if (pmm_frame_index(phys_addr, &frame_index) != 0)
	{
#if DEBUG_PMM
		serial_print("[PMM] WARN: Invalid address in put\n");
#endif
		return;
	}

	if (!bitmap_test(frame_index))
	{
		pmm_diag_double_free++;
#if DEBUG_PMM
		serial_print("[PMM] WARN: Double free detected\n");
#endif
		return;
	}

	if (pmm_frame_refs && pmm_frame_refs[frame_index] > 1)
	{
		pmm_frame_refs[frame_index]--;
		return;
	}

	if (pmm_frame_refs)
		pmm_frame_refs[frame_index] = 0;
	bitmap_clear(frame_index);
	pmm.used_frames--;
	if (pmm_frame_owner)
		pmm_frame_owner[frame_index] = -1;
	if (frame_index < (size_t)pmm_search_hint)
		pmm_search_hint = 0;
	if (pmm_diag_events < 2048U && IR0_DEBUG_PMM)
		pmm_diag_events++;
#if DEBUG_PMM
	serial_print("[PMM] Freed frame\n");
#endif
}

unsigned pmm_frame_refcount(uintptr_t phys_addr)
{
	size_t frame_index;

	if (pmm_frame_index(phys_addr, &frame_index) != 0)
		return 0;
	if (!bitmap_test(frame_index) || !pmm_frame_refs)
		return 0;
	return (unsigned)pmm_frame_refs[frame_index];
}

void pmm_free_frame(uintptr_t phys_addr)
{
	pmm_frame_put(phys_addr);
}

/*
 * pmm_get_start - Physical address of the first byte in the PMM-managed RAM region.
 */
uintptr_t pmm_get_start(void)
{
    return pmm.mem_start;
}

/*
 * pmm_get_end - Physical address just past the last managed byte (exclusive bound).
 */
uintptr_t pmm_get_end(void)
{
    return pmm.mem_end;
}

void pmm_stats(size_t *total_frames, size_t *used_frames, size_t *free_frames)
{
    if (!pmm.initialized)
        return;
    
    if (total_frames)
        *total_frames = pmm.total_frames;
    
    if (used_frames)
        *used_frames = pmm.used_frames;
    
    if (free_frames)
        *free_frames = pmm.total_frames - pmm.used_frames;

#if DEBUG_PMM
    serial_print("[PMM STATS]\n");
    serial_print("  Stats available via debugger\n");
    /* pmm.total_frames, pmm.used_frames available in GDB */
#endif
}

void pmm_owner_audit(uint64_t *orphan_frames, uint64_t *double_free,
                     uint64_t *alive_owner_missing)
{
    uint64_t orphan = 0;
    uint64_t alive_missing = 0;

    if (!pmm.initialized)
        return;

    if (pmm_frame_owner)
    {
        for (size_t i = 0; i < pmm.total_frames; i++)
        {
            if (!bitmap_test(i))
                continue;

            if (pmm_frame_owner[i] < 0)
            {
                orphan++;
                continue;
            }

            if (pmm_frame_owner[i] > 0 &&
                process_find_by_pid((pid_t)pmm_frame_owner[i]) == NULL)
            {
                alive_missing++;
            }
        }
    }

    if (orphan_frames)
        *orphan_frames = orphan;
    if (double_free)
        *double_free = pmm_diag_double_free;
    if (alive_owner_missing)
        *alive_owner_missing = alive_missing;
}

int pmm_fase47_frame_is_used(size_t frame_index)
{
    if (!pmm.initialized || frame_index >= pmm.total_frames)
        return 0;
    return bitmap_test(frame_index) ? 1 : 0;
}

int32_t pmm_fase47_frame_owner(size_t frame_index)
{
    if (!pmm.initialized || frame_index >= pmm.total_frames || !pmm_frame_owner)
        return -1;
    return pmm_frame_owner[frame_index];
}

uintptr_t pmm_fase47_frame_phys(size_t frame_index)
{
    if (!pmm.initialized || frame_index >= pmm.total_frames)
        return 0;
    return pmm.mem_start + (uintptr_t)(frame_index * PMM_FRAME_SIZE);
}

size_t pmm_fase47_total_frames(void)
{
    if (!pmm.initialized)
        return 0;
    return pmm.total_frames;
}
