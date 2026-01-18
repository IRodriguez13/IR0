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

#if DEBUG_PMM
    serial_print("[PMM] Initialized\n");
#endif
}

uintptr_t pmm_alloc_frame(void)
{
    if (!pmm.initialized)
        return 0;
    
    /* First-fit: find first free frame */
    for (size_t i = 0; i < pmm.total_frames; i++)
    {
        if (!bitmap_test(i))
        {
            /* Found free frame - mark as used */
            bitmap_set(i);
            pmm.used_frames++;
            
            /* Calculate physical address */
            uintptr_t phys_addr = pmm.mem_start + (i * PMM_FRAME_SIZE);
            
#if DEBUG_PMM
            serial_print("[PMM] Allocated frame\n");
#endif
            
            return phys_addr;
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
