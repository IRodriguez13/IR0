/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: paging_audit.c
 * Description: Page-table and frame accounting, OOM classification and steady-state audits.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include <config.h>
#include <ir0/logging.h>
#include <ir0/oops.h>
#include <ir0/process.h>
#include <ir0/mm.h>
#include <ir0/debug_runtime.h>
#include <ir0/mm_alloc.h>
#include <ir0/page_table_pool.h>
#include "paging.h"
#include <string.h>
#include <ir0/kmem.h>
#include <mm/pmm.h>
#include <ir0/validation.h>
#include <ir0/arch_port.h>
#include <ir0/tlb_sync.h>
#include <ir0/errno.h>
#include <ir0/swap.h>
#include <ir0/arch_cpu.h>
#include "paging_internal.h"

uint64_t ir0_mm_root_created;
uint64_t ir0_mm_root_freed;
uint64_t ir0_mm_level1_created;
uint64_t ir0_mm_level1_freed;
uint64_t ir0_mm_level2_created;
uint64_t ir0_mm_level2_freed;
uint64_t ir0_mm_level3_created;
uint64_t ir0_mm_level3_freed;
uint64_t ir0_mm_leaf_created;
uint64_t ir0_mm_leaf_freed;

uint64_t ir0_mm_frame_user_alloc;
uint64_t ir0_mm_frame_user_free;
uint64_t ir0_mm_frame_pt_alloc;
uint64_t ir0_mm_frame_pt_free;
uint64_t ir0_mm_frame_kernel_alloc;
uint64_t ir0_mm_frame_kernel_free;

static uint8_t *ir0_mm_frame_type_map;
static size_t ir0_mm_frame_type_map_frames;
#if IR0_DEBUG_PROC
static uint32_t ir0_mm_frame_log_events;
#endif

static uint64_t paging_oom_boot_fatal;
static uint64_t paging_oom_kernel_fatal;
static uint64_t paging_oom_user_recoverable;

static paging_oom_class_t paging_classify_oom(void)
{
    if (current_process && current_process->mode == USER_MODE)
        return PAGING_OOM_USER_RECOVERABLE;
    if (!current_process)
        return PAGING_OOM_BOOT_FATAL;
    return PAGING_OOM_KERNEL_FATAL;
}

void paging_oom_note(const char *site, paging_oom_class_t cls)
{
    (void)site;
    switch (cls)
    {
    case PAGING_OOM_BOOT_FATAL:
        paging_oom_boot_fatal++;
        break;
    case PAGING_OOM_KERNEL_FATAL:
        paging_oom_kernel_fatal++;
        break;
    case PAGING_OOM_USER_RECOVERABLE:
        paging_oom_user_recoverable++;
        break;
    default:
        break;
    }
}

void paging_oom_audit(const char *tag)
{
    (void)tag;
}

paging_oom_class_t paging_oom_classify_current(void)
{
    return paging_classify_oom();
}

static int ir0_mm_frame_type_ensure(void)
{
    size_t total_frames = 0;

    if (ir0_mm_frame_type_map)
        return 0;

    pmm_stats(&total_frames, NULL, NULL);
    if (total_frames == 0)
        return -1;

    ir0_mm_frame_type_map = kmalloc_try(total_frames);
    if (!ir0_mm_frame_type_map)
        return -1;

    memset(ir0_mm_frame_type_map, 0, total_frames);
    ir0_mm_frame_type_map_frames = total_frames;
    return 0;
}

static long ir0_mm_frame_index(uint64_t phys_addr)
{
    uintptr_t start = pmm_get_start();
    uintptr_t end = pmm_get_end();

    if (phys_addr < start || phys_addr >= end)
        return -1;
    return (ssize_t)((phys_addr - start) / PAGE_SIZE_4KB);
}

ir0_mm_frame_type_t ir0_mm_get_frame_type(uint64_t phys_addr)
{
    long idx;

    if (ir0_mm_frame_type_ensure() != 0)
        return IR0_MM_FRAME_UNKNOWN;

    idx = ir0_mm_frame_index(phys_addr);
    if (idx < 0 || (size_t)idx >= ir0_mm_frame_type_map_frames)
        return IR0_MM_FRAME_UNKNOWN;
    return (ir0_mm_frame_type_t)ir0_mm_frame_type_map[idx];
}

void ir0_mm_set_frame_type(uint64_t phys_addr, ir0_mm_frame_type_t t)
{
    long idx;

    if (ir0_mm_frame_type_ensure() != 0)
        return;
    idx = ir0_mm_frame_index(phys_addr);
    if (idx < 0 || (size_t)idx >= ir0_mm_frame_type_map_frames)
        return;
    ir0_mm_frame_type_map[idx] = (uint8_t)t;
}

__attribute__((unused))
static const char *ir0_mm_frame_type_name(ir0_mm_frame_type_t t)
{
    switch (t)
    {
    case IR0_MM_FRAME_USER:
        return "FRAME_USER";
    case IR0_MM_FRAME_PT:
        return "FRAME_PT";
    case IR0_MM_FRAME_KERNEL:
        return "FRAME_KERNEL";
    default:
        return "FRAME_UNKNOWN";
    }
}

void ir0_mm_log_frame_type(const char *kind, uint64_t frame, ir0_mm_frame_type_t t)
{
#if !IR0_DEBUG_PROC
    (void)kind;
    (void)frame;
    (void)t;
    return;
#else
    if (ir0_mm_frame_log_events >= 2048U)
        return;

    ir0_mm_frame_log_events++;
#endif
}

void paging_ir0_mm_note_root_created(uintptr_t root_phys)
{
    ir0_mm_root_created++;
    ir0_mm_set_frame_type(root_phys, IR0_MM_FRAME_PT);
    ir0_mm_frame_pt_alloc++;
    ir0_mm_log_frame_type("ALLOC", root_phys, IR0_MM_FRAME_PT);
}

void paging_ir0_mm_note_root_freed(uintptr_t root_phys)
{
    ir0_mm_root_freed++;
    ir0_mm_set_frame_type(root_phys, IR0_MM_FRAME_UNKNOWN);
    ir0_mm_frame_pt_free++;
    ir0_mm_log_frame_type("FREE", root_phys, IR0_MM_FRAME_PT);
}

void paging_ir0_mm_checkpoint(const char *tag, int32_t pid)
{
#if !IR0_DEBUG_PROC
    (void)tag;
    (void)pid;
    return;
#else

#endif
}

void paging_ir0_mm_category_stats(uint64_t *user_alloc, uint64_t *user_free,
                                  uint64_t *pt_alloc, uint64_t *pt_free,
                                  uint64_t *kernel_alloc, uint64_t *kernel_free)
{
    if (user_alloc)
        *user_alloc = ir0_mm_frame_user_alloc;
    if (user_free)
        *user_free = ir0_mm_frame_user_free;
    if (pt_alloc)
        *pt_alloc = ir0_mm_frame_pt_alloc;
    if (pt_free)
        *pt_free = ir0_mm_frame_pt_free;
    if (kernel_alloc)
        *kernel_alloc = ir0_mm_frame_kernel_alloc;
    if (kernel_free)
        *kernel_free = ir0_mm_frame_kernel_free;
}

static ir0_mm_frame_type_t paging_frame_type(uintptr_t phys)
{
    return ir0_mm_get_frame_type((uint64_t)phys);
}

void paging_steady_state_audit(const char *tag, uint64_t frames_baseline,
                              uint64_t mm_created, uint64_t mm_destroyed)
{
    size_t total_frames = 0;
    size_t used_frames = 0;
    size_t i;
    size_t nframes;
    uint64_t alive_user_leaf = 0;
    uint64_t alive_page_table = 0;
    uint64_t alive_kernel_heap = 0;
    uint64_t alive_process_struct = 0;
    uint64_t alive_vma_meta = 0;
    uint64_t alive_file_cache = 0;
    uint64_t alive_unknown = 0;
    uint64_t leaf_alive;
    uint64_t mm_alive;
    uint64_t orphan_frames = 0;
    uint64_t double_free = 0;
    uint64_t alive_owner_missing = 0;
    int inv_leaf;
    int inv_mm;
    int inv_frames;
    int class_steady;
    int class_resident;
    int class_accounting;
    int class_leak;
    const char *memory_class;

    pmm_stats(&total_frames, &used_frames, NULL);
    nframes = pmm_total_frames();

    for (i = 0; i < nframes; i++)
    {
        ir0_mm_frame_type_t ft;
        int32_t owner;
        uintptr_t phys;

        if (!pmm_frame_is_used(i))
            continue;

        phys = pmm_frame_phys(i);
        ft = paging_frame_type(phys);
        owner = pmm_frame_owner(i);

        if (ft == IR0_MM_FRAME_USER)
            alive_user_leaf++;
        else if (ft == IR0_MM_FRAME_PT)
            alive_page_table++;
        else if (ft == IR0_MM_FRAME_KERNEL)
        {
            if (owner > 0)
                alive_process_struct++;
            else
                alive_kernel_heap++;
        }
        else
            alive_unknown++;
    }

    leaf_alive = (ir0_mm_leaf_created >= ir0_mm_leaf_freed) ?
                 (ir0_mm_leaf_created - ir0_mm_leaf_freed) : 0;
    mm_alive = (mm_created >= mm_destroyed) ?
               (mm_created - mm_destroyed) : 0;

    inv_leaf = (ir0_mm_leaf_created == ir0_mm_leaf_freed + leaf_alive);
    inv_mm = (mm_created == mm_destroyed + mm_alive);
    inv_frames = (frames_baseline == 0 ||
                  (uint64_t)used_frames <= (frames_baseline * 110ULL / 100ULL));

    pmm_owner_audit(&orphan_frames, &double_free, &alive_owner_missing);

    (void)alive_user_leaf;
    (void)alive_page_table;
    (void)alive_kernel_heap;
    (void)alive_process_struct;
    (void)alive_vma_meta;
    (void)alive_file_cache;
    (void)alive_unknown;
    (void)double_free;
    (void)tag;

    class_accounting = (!inv_leaf || !inv_mm);
    class_leak = (orphan_frames > 0 || alive_owner_missing > 0 ||
                  (!inv_leaf && leaf_alive > alive_user_leaf + alive_page_table));
    class_steady = (inv_leaf && inv_mm && inv_frames && !class_leak);
    class_resident = (!inv_frames && inv_leaf && inv_mm && !class_leak);

    if (class_steady)
        memory_class = "MEMORY_STEADY_OK";
    else if (class_accounting)
        memory_class = "MEMORY_ACCOUNTING_BROKEN";
    else if (class_leak)
        memory_class = "MEMORY_LEAK_CONFIRMED";
    else if (class_resident)
        memory_class = "MEMORY_RESIDENT_EXPECTED";
    else
        memory_class = "MEMORY_RESIDENT_EXPECTED";
    (void)memory_class;
}
