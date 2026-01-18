// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: pmm.h
 * Description: Physical Memory Manager - Bitmap-based frame allocator
 *              Manages 4KB physical frames for paging subsystem
 */

#pragma once

#include <stdint.h>
#include <stddef.h>


/* Physical frame size (4KB pages) */
#define PMM_FRAME_SIZE 4096


/**
 * pmm_init - Initialize Physical Memory Manager
 * @mem_start: Start of physical memory region to manage
 * @mem_size: Size of physical memory region in bytes
 *
 * Initializes the PMM with a bitmap covering the specified memory region.
 * The bitmap itself is allocated from kernel heap via kmalloc.
 */
void pmm_init(uintptr_t mem_start, size_t mem_size);

/**
 * pmm_alloc_frame - Allocate a physical frame
 *
 * Returns: Physical address of allocated 4KB frame, or 0 on failure
 *
 * Uses first-fit algorithm to find free frame in bitmap.
 * Marks frame as used and returns its physical address.
 */
uintptr_t pmm_alloc_frame(void);

/**
 * pmm_free_frame - Free a physical frame
 * @phys_addr: Physical address of frame to free
 *
 * Marks the frame containing phys_addr as free in the bitmap.
 * Address is validated to be within managed memory region.
 */
void pmm_free_frame(uintptr_t phys_addr);

/**
 * pmm_stats - Get PMM statistics
 * @total_frames: Output for total number of frames
 * @used_frames: Output for number of allocated frames
 * @free_frames: Output for number of free frames
 *
 * Retrieves current PMM statistics for debugging.
 * Any parameter can be NULL if that stat is not needed.
 */
void pmm_stats(size_t *total_frames, size_t *used_frames, size_t *free_frames);
