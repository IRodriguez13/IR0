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
#include <ir0/klog.h>
#include <ir0/process.h>
#include <ir0/debug_runtime.h>
#include <ir0/arch_cpu.h>
#include <ir0/oops.h>
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
static uint8_t *pmm_owner_class_tab;
static int32_t *pmm_frame_owner_pid;
static uint32_t *pmm_frame_refs;
static uint64_t pmm_diag_double_free;
static uint32_t pmm_diag_events;

static inline unsigned long pmm_lock(void)
{
	return irq_save();
}

static inline void pmm_unlock(unsigned long flags)
{
	irq_restore(flags);
}

static void pmm_panic_mem(const char *msg)
{
	panicex(msg, PANIC_MEM, __FILE__, __LINE__, __func__);
}

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

/* BITMAP OPERATIONS (caller must hold pmm_lock)                            */

static inline void bitmap_set(size_t frame_index)
{
	pmm.bitmap[frame_index / 8] |= (1 << (frame_index % 8));
}

static inline void bitmap_clear(size_t frame_index)
{
	pmm.bitmap[frame_index / 8] &= ~(1 << (frame_index % 8));
}

static inline int bitmap_test(size_t frame_index)
{
	return (pmm.bitmap[frame_index / 8] & (1 << (frame_index % 8))) != 0;
}

static void pmm_owner_clear_locked(size_t frame_index)
{
	if (pmm_owner_class_tab)
		pmm_owner_class_tab[frame_index] = PMM_OWNER_NONE;
	if (pmm_frame_owner_pid)
		pmm_frame_owner_pid[frame_index] = -1;
}

static void pmm_owner_on_alloc_locked(size_t frame_index)
{
	pmm_owner_class_t cls = PMM_OWNER_KERNEL;
	int32_t pid = -1;

	if (current_process && current_process->mode == USER_MODE)
	{
		pid_t owner_pid = process_get_pid();

		if (owner_pid > 0)
		{
			cls = PMM_OWNER_USER;
			pid = (int32_t)owner_pid;
		}
	}

	if (pmm_owner_class_tab)
		pmm_owner_class_tab[frame_index] = (uint8_t)cls;
	if (pmm_frame_owner_pid)
		pmm_frame_owner_pid[frame_index] = pid;
}

static void pmm_owner_on_get_locked(size_t frame_index)
{
	uint32_t refs;

	if (!pmm_owner_class_tab || !pmm_frame_refs)
		return;

	refs = pmm_frame_refs[frame_index];
	if (refs >= 2 &&
	    pmm_owner_class_tab[frame_index] == PMM_OWNER_USER)
	{
		pmm_owner_class_tab[frame_index] = PMM_OWNER_SHARED;
		if (pmm_frame_owner_pid)
			pmm_frame_owner_pid[frame_index] = -1;
	}
}

static void pmm_owner_set_locked(size_t frame_index, pmm_owner_class_t cls,
                                 int32_t pid)
{
	uint32_t refs = 0;

	if (!pmm_owner_class_tab)
		return;

	if (pmm_frame_refs)
		refs = pmm_frame_refs[frame_index];

	if (refs >= 2 && cls == PMM_OWNER_USER)
		cls = PMM_OWNER_SHARED;

	if (cls == PMM_OWNER_SHARED || refs >= 2)
	{
		pmm_owner_class_tab[frame_index] = PMM_OWNER_SHARED;
		if (pmm_frame_owner_pid)
			pmm_frame_owner_pid[frame_index] = -1;
		return;
	}

	pmm_owner_class_tab[frame_index] = (uint8_t)cls;
	if (pmm_frame_owner_pid)
	{
		if (cls == PMM_OWNER_USER && pid > 0)
			pmm_frame_owner_pid[frame_index] = pid;
		else
			pmm_frame_owner_pid[frame_index] = -1;
	}
}

void pmm_init(uintptr_t mem_start, size_t mem_size)
{
	size_t i;

	if (pmm.initialized)
		return;

	/* Align to frame boundaries */
	pmm.mem_start = (mem_start + PMM_FRAME_SIZE - 1) & ~(PMM_FRAME_SIZE - 1);
	pmm.mem_end = (mem_start + mem_size) & ~(PMM_FRAME_SIZE - 1);

	/* Calculate total frames */
	pmm.total_frames = (pmm.mem_end - pmm.mem_start) / PMM_FRAME_SIZE;

	/* Allocate bitmap (1 bit per frame) */
	{
		size_t bitmap_size = (pmm.total_frames + 7) / 8;

		pmm.bitmap = kmalloc(bitmap_size);

		if (!pmm.bitmap)
		{
#if DEBUG_PMM
			klog_print("[PMM] CRITICAL: Failed to allocate bitmap\n");
#endif
			return;
		}

		/* Initialize all frames as free (bitmap = 0) */
		for (i = 0; i < bitmap_size; i++)
			pmm.bitmap[i] = 0;
	}

	pmm.used_frames = 0;
	pmm.initialized = 1;
	pmm_search_hint = 0;
	pmm_diag_double_free = 0;
	pmm_diag_events = 0;

	pmm_owner_class_tab = kmalloc(pmm.total_frames * sizeof(uint8_t));
	if (pmm_owner_class_tab)
	{
		for (i = 0; i < pmm.total_frames; i++)
			pmm_owner_class_tab[i] = PMM_OWNER_NONE;
	}

	pmm_frame_owner_pid = kmalloc(pmm.total_frames * sizeof(int32_t));
	if (pmm_frame_owner_pid)
	{
		for (i = 0; i < pmm.total_frames; i++)
			pmm_frame_owner_pid[i] = -1;
	}

	pmm_frame_refs = kmalloc(pmm.total_frames * sizeof(uint32_t));
	if (pmm_frame_refs)
	{
		for (i = 0; i < pmm.total_frames; i++)
			pmm_frame_refs[i] = 0;
	}

#if DEBUG_PMM
	klog_print("[PMM] Initialized\n");
#endif
}

uintptr_t pmm_alloc_frame(void)
{
	unsigned long irq_flags;
	uintptr_t ret = 0;

	if (!pmm.initialized)
		return 0;

	irq_flags = pmm_lock();

	/* First-fit from search hint, then wrap [0, hint) */
	{
		size_t start = (size_t)pmm_search_hint;
		size_t pass;

		if (start >= pmm.total_frames)
			start = 0;

		for (pass = 0; pass < 2 && ret == 0; pass++)
		{
			size_t i = (pass == 0) ? start : 0;
			size_t end = (pass == 0) ? pmm.total_frames : start;

			for (; i < end; i++)
			{
				if (!bitmap_test(i))
				{
					bitmap_set(i);
					pmm.used_frames++;
					pmm_search_hint = (uint32_t)((i + 1) %
					                             pmm.total_frames);
					if (pmm_frame_refs)
						pmm_frame_refs[i] = 1;
					pmm_owner_on_alloc_locked(i);
					if (pmm_diag_events < 2048U && IR0_DEBUG_PMM)
						pmm_diag_events++;

					ret = pmm.mem_start + (i * PMM_FRAME_SIZE);
					break;
				}
			}
		}
	}

	pmm_unlock(irq_flags);

	if (ret == 0)
	{
#if DEBUG_PMM
		klog_print("[PMM] FAILED: Out of physical memory\n");
#endif
	}

	return ret;
}

void pmm_frame_get(uintptr_t phys_addr)
{
	unsigned long irq_flags;
	size_t frame_index;

	if (pmm_frame_index(phys_addr, &frame_index) != 0)
		return;

	irq_flags = pmm_lock();

	if (!bitmap_test(frame_index))
	{
		pmm_unlock(irq_flags);
		return;
	}

	if (!pmm_frame_refs)
	{
		pmm_unlock(irq_flags);
		return;
	}

	if (pmm_frame_refs[frame_index] >= UINT32_MAX)
		pmm_panic_mem("PMM: refcount overflow on frame_get");

	pmm_frame_refs[frame_index]++;
	pmm_owner_on_get_locked(frame_index);

	pmm_unlock(irq_flags);
}

void pmm_frame_put(uintptr_t phys_addr)
{
	unsigned long irq_flags;
	size_t frame_index;

	if (pmm_frame_index(phys_addr, &frame_index) != 0)
	{
#if DEBUG_PMM
		klog_print("[PMM] WARN: Invalid address in put\n");
#endif
		return;
	}

	irq_flags = pmm_lock();

	if (!bitmap_test(frame_index))
	{
		pmm_diag_double_free++;
		pmm_unlock(irq_flags);
		pmm_panic_mem("PMM: double free detected");
		return;
	}

	if (pmm_frame_refs)
	{
		if (pmm_frame_refs[frame_index] == 0)
		{
			pmm_unlock(irq_flags);
			pmm_panic_mem("PMM: put with zero refcount on used frame");
			return;
		}

		if (pmm_frame_refs[frame_index] > 1)
		{
			pmm_frame_refs[frame_index]--;
			pmm_unlock(irq_flags);
			return;
		}

		pmm_frame_refs[frame_index] = 0;
	}

	bitmap_clear(frame_index);
	pmm.used_frames--;
	pmm_owner_clear_locked(frame_index);
	if (frame_index < (size_t)pmm_search_hint)
		pmm_search_hint = 0;
	if (pmm_diag_events < 2048U && IR0_DEBUG_PMM)
		pmm_diag_events++;

	pmm_unlock(irq_flags);

#if DEBUG_PMM
	klog_print("[PMM] Freed frame\n");
#endif
}

unsigned pmm_frame_refcount(uintptr_t phys_addr)
{
	unsigned long irq_flags;
	unsigned refs = 0;
	size_t frame_index;

	if (pmm_frame_index(phys_addr, &frame_index) != 0)
		return 0;

	irq_flags = pmm_lock();

	if (bitmap_test(frame_index) && pmm_frame_refs)
		refs = (unsigned)pmm_frame_refs[frame_index];

	pmm_unlock(irq_flags);
	return refs;
}

void pmm_frame_set_owner_class(uintptr_t phys_addr, pmm_owner_class_t cls,
                               int32_t pid)
{
	unsigned long irq_flags;
	size_t frame_index;

	if (pmm_frame_index(phys_addr, &frame_index) != 0)
		return;

	irq_flags = pmm_lock();

	if (!bitmap_test(frame_index))
	{
		pmm_unlock(irq_flags);
		return;
	}

	pmm_owner_set_locked(frame_index, cls, pid);

	pmm_unlock(irq_flags);
}

pmm_owner_class_t pmm_frame_owner_class(uintptr_t phys_addr)
{
	unsigned long irq_flags;
	pmm_owner_class_t cls = PMM_OWNER_NONE;
	size_t frame_index;

	if (pmm_frame_index(phys_addr, &frame_index) != 0)
		return PMM_OWNER_NONE;

	irq_flags = pmm_lock();

	if (bitmap_test(frame_index) && pmm_owner_class_tab)
		cls = (pmm_owner_class_t)pmm_owner_class_tab[frame_index];

	pmm_unlock(irq_flags);
	return cls;
}

void pmm_free_frame(uintptr_t phys_addr)
{
	pmm_frame_put(phys_addr);
}

uintptr_t pmm_get_start(void)
{
	return pmm.mem_start;
}

uintptr_t pmm_get_end(void)
{
	return pmm.mem_end;
}

void pmm_stats(size_t *total_frames, size_t *used_frames, size_t *free_frames)
{
	unsigned long irq_flags;

	if (!pmm.initialized)
		return;

	irq_flags = pmm_lock();

	if (total_frames)
		*total_frames = pmm.total_frames;

	if (used_frames)
		*used_frames = pmm.used_frames;

	if (free_frames)
		*free_frames = pmm.total_frames - pmm.used_frames;

	pmm_unlock(irq_flags);

#if DEBUG_PMM
	klog_print("[PMM STATS]\n");
	klog_print("  Stats available via debugger\n");
#endif
}

void pmm_owner_audit(uint64_t *orphan_frames, uint64_t *double_free,
                     uint64_t *alive_owner_missing)
{
	unsigned long irq_flags;
	uint64_t orphan = 0;
	uint64_t alive_missing = 0;
	size_t i;

	if (!pmm.initialized)
		return;

	irq_flags = pmm_lock();

	if (pmm_owner_class_tab)
	{
		for (i = 0; i < pmm.total_frames; i++)
		{
			pmm_owner_class_t cls;

			if (!bitmap_test(i))
				continue;

			cls = (pmm_owner_class_t)pmm_owner_class_tab[i];

			if (cls == PMM_OWNER_NONE)
			{
				orphan++;
				continue;
			}

			if (cls == PMM_OWNER_USER && pmm_frame_owner_pid &&
			    pmm_frame_owner_pid[i] > 0 &&
			    process_find_by_pid((pid_t)pmm_frame_owner_pid[i]) == NULL)
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

	pmm_unlock(irq_flags);
}

int pmm_fase47_frame_is_used(size_t frame_index)
{
	unsigned long irq_flags;
	int used = 0;

	if (!pmm.initialized || frame_index >= pmm.total_frames)
		return 0;

	irq_flags = pmm_lock();
	used = bitmap_test(frame_index) ? 1 : 0;
	pmm_unlock(irq_flags);
	return used;
}

pmm_owner_class_t pmm_fase47_frame_owner_class(size_t frame_index)
{
	unsigned long irq_flags;
	pmm_owner_class_t cls = PMM_OWNER_NONE;

	if (!pmm.initialized || frame_index >= pmm.total_frames ||
	    !pmm_owner_class_tab)
		return PMM_OWNER_NONE;

	irq_flags = pmm_lock();
	if (bitmap_test(frame_index))
		cls = (pmm_owner_class_t)pmm_owner_class_tab[frame_index];
	pmm_unlock(irq_flags);
	return cls;
}

int32_t pmm_fase47_frame_owner(size_t frame_index)
{
	unsigned long irq_flags;
	int32_t owner = -1;

	if (!pmm.initialized || frame_index >= pmm.total_frames ||
	    !pmm_frame_owner_pid)
		return -1;

	irq_flags = pmm_lock();

	if (bitmap_test(frame_index) && pmm_owner_class_tab &&
	    pmm_owner_class_tab[frame_index] == PMM_OWNER_USER &&
	    pmm_frame_owner_pid)
	{
		owner = pmm_frame_owner_pid[frame_index];
	}

	pmm_unlock(irq_flags);
	return owner;
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
