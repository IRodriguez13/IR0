// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: pmm.c
 * Description: Physical Memory Manager implementation
 *              Simple bitmap-based allocator for 4KB physical frames
 */

#include "pmm.h"
#include <ir0/kmem.h>
#include <drivers/serial/serial.h>
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
                bitmap_set(i);
                pmm.used_frames++;
                pmm_search_hint = (uint32_t)((i + 1) % pmm.total_frames);

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

void pmm_free_frame(uintptr_t phys_addr)
{
    if (!pmm.initialized)
        return;
    
    /* Validate address is within managed region */
    if (phys_addr < pmm.mem_start || phys_addr >= pmm.mem_end)
    {
#if DEBUG_PMM
        serial_print("[PMM] WARN: Invalid address in free\n");
#endif
        return;
    }
    
    /* Align to frame boundary */
    phys_addr &= ~(PMM_FRAME_SIZE - 1);
    
    /* Calculate frame index */
    size_t frame_index = (phys_addr - pmm.mem_start) / PMM_FRAME_SIZE;
    
    /* Only free if currently used */
    if (bitmap_test(frame_index))
    {
        bitmap_clear(frame_index);
        pmm.used_frames--;

        if (frame_index < (size_t)pmm_search_hint)
            pmm_search_hint = 0;
        
#if DEBUG_PMM
        serial_print("[PMM] Freed frame\n");
#endif
    }
    else
    {
#if DEBUG_PMM
        serial_print("[PMM] WARN: Double free detected\n");
#endif
    }
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
