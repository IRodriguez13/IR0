/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: swap_reclaim.c
 * Description: Anonymous swap-out/in, PTE lifecycle, and process-context reclaim.
 */

#include "swap_internal.h"

#include <ir0/abi/mmap_contract.h>
#include <ir0/arch_mm.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/paging.h>
#include <ir0/pmm.h>
#include <ir0/process.h>
#include <ir0/swap.h>
#include <ir0/tlb_sync.h>
#include <mm/paging.h>
#include <string.h>

#ifndef TEST_HOST

static uintptr_t g_emergency[2];
static uint8_t g_emergency_used[2];
static uintptr_t g_bounce_phys;

static uintptr_t alloc_frame_or_reserve(void)
{
	uintptr_t frame = pmm_alloc_frame();
	unsigned i;

	if (frame)
		return frame;
	for (i = 0; i < 2U; i++)
	{
		if (g_emergency[i] && !g_emergency_used[i])
		{
			g_emergency_used[i] = 1;
			return g_emergency[i];
		}
	}
	return 0;
}

static void free_frame_or_reserve(uintptr_t frame)
{
	unsigned i;

	for (i = 0; i < 2U; i++)
	{
		if (frame == g_emergency[i])
		{
			g_emergency_used[i] = 0;
			return;
		}
	}
	pmm_free_frame(frame);
}

static void ensure_bounce(void)
{
	if (!g_bounce_phys)
		g_bounce_phys = pmm_alloc_frame();
	if (!g_emergency[0])
		g_emergency[0] = pmm_alloc_frame();
	if (!g_emergency[1])
		g_emergency[1] = pmm_alloc_frame();
}

int ir0_swap_dup_pte(uint64_t pte)
{
	unsigned type;
	uint64_t offset;

	if (!mm_pte_is_swap(pte))
		return 0;
	mm_pte_swap_decode(pte, &type, &offset);
	return ir0_swap_slot_get(type, offset);
}

int ir0_swap_drop_pte(uint64_t pte)
{
	unsigned type;
	uint64_t offset;

	if (!mm_pte_is_swap(pte))
		return 0;
	mm_pte_swap_decode(pte, &type, &offset);
	return ir0_swap_slot_put(type, offset);
}

int ir0_swap_out_page(process_t *proc, uintptr_t va)
{
	uint64_t *pte;
	uintptr_t phys;
	unsigned type;
	uint64_t offset;
	int rc;

	if (!proc || !process_pgd(proc))
		return -EINVAL;
	ensure_bounce();
	if (!g_bounce_phys)
		return -ENOMEM;
	va &= ~(uintptr_t)(PAGE_SIZE_4KB - 1U);
	pte = paging_get_pte(process_pgd(proc), va);
	if (!pte || !mm_pte_present(*pte) || !(*pte & PAGE_USER))
		return -ENOENT;
	phys = mm_pte_phys(*pte);
	if (pmm_frame_refcount(phys) != 1)
		return -EBUSY;
	rc = ir0_swap_slot_alloc(&type, &offset);
	if (rc < 0)
		return rc;
	paging_copy_phys_page(g_bounce_phys, phys);
	rc = ir0_swap_write_page(type, offset, (void *)g_bounce_phys);
	if (rc < 0)
	{
		(void)ir0_swap_slot_put(type, offset);
		return rc;
	}
	*pte = mm_make_swap_pte(type, offset);
	mm_tlb_sync_page(process_pgd(proc), va);
	pmm_frame_put(phys);
	return 0;
}

int ir0_swap_in_page(process_t *proc, uintptr_t va, uint64_t map_flags)
{
	uint64_t *pte;
	unsigned type;
	uint64_t offset;
	uintptr_t frame;
	int rc;

	if (!proc || !process_pgd(proc))
		return -EINVAL;
	ensure_bounce();
	if (!g_bounce_phys)
		return -ENOMEM;
	va &= ~(uintptr_t)(PAGE_SIZE_4KB - 1U);
	pte = paging_get_pte(process_pgd(proc), va);
	if (!pte || !mm_pte_is_swap(*pte))
		return -ENOENT;
	mm_pte_swap_decode(*pte, &type, &offset);
	rc = ir0_swap_read_page(type, offset, (void *)g_bounce_phys);
	if (rc < 0)
		return rc;
	frame = alloc_frame_or_reserve();
	if (!frame)
		return -ENOMEM;
	paging_copy_phys_page(frame, g_bounce_phys);
	if (map_flags == 0)
		map_flags = PAGE_USER | PAGE_RW;
	rc = map_page_in_directory(process_pgd(proc), va, frame, map_flags);
	if (rc < 0)
	{
		free_frame_or_reserve(frame);
		return rc;
	}
	(void)ir0_swap_slot_put(type, offset);
	mm_tlb_sync_page(process_pgd(proc), va);
	return 0;
}

static int try_range(process_t *proc, uintptr_t start, uintptr_t end,
		     int swap_in, unsigned want_type, uint64_t *reclaimed)
{
	uintptr_t va;

	if (end <= start)
		return 0;
	start &= ~(uintptr_t)(PAGE_SIZE_4KB - 1U);
	for (va = start; va < end; va += PAGE_SIZE_4KB)
	{
		uint64_t *pte = paging_get_pte(process_pgd(proc), va);

		if (!pte)
			continue;
		if (swap_in)
		{
			unsigned type;
			uint64_t off;

			if (!mm_pte_is_swap(*pte))
				continue;
			mm_pte_swap_decode(*pte, &type, &off);
			if (want_type != (unsigned)-1 && type != want_type)
				continue;
			if (ir0_swap_in_page(proc, va, PAGE_USER | PAGE_RW) == 0)
			{
				if (reclaimed)
					(*reclaimed)++;
				return 1;
			}
		}
		else if (mm_pte_present(*pte) && (*pte & PAGE_USER) &&
			 ir0_swap_out_page(proc, va) == 0)
		{
			if (reclaimed)
				(*reclaimed)++;
			return 1;
		}
	}
	return 0;
}

static int walk_process(process_t *proc, int swap_in, unsigned want_type,
			uint64_t *reclaimed)
{
	struct mmap_region *r;
	uintptr_t stack_lo;
	uintptr_t stack_hi;

	if (!proc || !process_pgd(proc))
		return 0;
	if (try_range(proc, (uintptr_t)process_heap_start(proc),
		      (uintptr_t)process_heap_end(proc), swap_in, want_type,
		      reclaimed))
		return 1;
	stack_lo = (uintptr_t)process_stack_start(proc);
	stack_hi = stack_lo + (uintptr_t)process_stack_size(proc);
	if (try_range(proc, stack_lo, stack_hi, swap_in, want_type, reclaimed))
		return 1;
	for (r = process_mmap_list(proc); r; r = r->next)
	{
		if ((r->flags & IR0_MAP_ANONYMOUS) == 0)
			continue;
		if (try_range(proc, (uintptr_t)r->addr,
			      (uintptr_t)r->addr + r->length, swap_in, want_type,
			      reclaimed))
			return 1;
	}
	return 0;
}

int ir0_swap_try_reclaim(uint64_t target, uint64_t *reclaimed)
{
	process_t *p;
	uint64_t got = 0;

	if (!reclaimed)
		return -EINVAL;
	*reclaimed = 0;
	if (ir0_swap_area_count() == 0)
		return -ENOSPC;
	ensure_bounce();
	p = process_list;
	while (p && got < target)
	{
		if (walk_process(p, 0, (unsigned)-1, &got) == 0)
			p = p->next;
	}
	*reclaimed = got;
	return got > 0 ? 0 : -ENOSPC;
}

int ir0_swap_drain_area(unsigned type)
{
	process_t *p;
	unsigned rounds = 0;
	uint64_t dummy;

	while (ir0_swap_slot_busy(type) && rounds < 64U)
	{
		int progress = 0;

		p = process_list;
		while (p)
		{
			dummy = 0;
			if (walk_process(p, 1, type, &dummy))
				progress = 1;
			p = p->next;
		}
		if (!progress)
			break;
		rounds++;
	}
	return ir0_swap_slot_busy(type) ? -EBUSY : 0;
}

#else /* TEST_HOST */

int ir0_swap_dup_pte(uint64_t pte)
{
	(void)pte;
	return 0;
}

int ir0_swap_drop_pte(uint64_t pte)
{
	(void)pte;
	return 0;
}

int ir0_swap_out_page(process_t *proc, uintptr_t va)
{
	(void)proc;
	(void)va;
	return -ENOSYS;
}

int ir0_swap_in_page(process_t *proc, uintptr_t va, uint64_t map_flags)
{
	(void)proc;
	(void)va;
	(void)map_flags;
	return -ENOSYS;
}

int ir0_swap_try_reclaim(uint64_t target, uint64_t *reclaimed)
{
	(void)target;
	if (reclaimed)
		*reclaimed = 0;
	return -ENOSPC;
}

int ir0_swap_drain_area(unsigned type)
{
	return ir0_swap_slot_busy(type) ? -EBUSY : 0;
}

#endif /* TEST_HOST */
