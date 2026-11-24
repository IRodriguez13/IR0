/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: va_search.c
 * Description: Free user virtual address search: top-down mmap placement and hint checks.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <mm/va_search.h>
#include <config.h>
#include <ir0/abi/mmap_contract.h>
#include <ir0/paging.h>
#include <ir0/process.h>

/*
 * Lowest mapped page in [start, start + length), or 0 when there is none:
 * every window that still contains it is skipped in one step.
 */
static uintptr_t mm_va_range_lowest_mapped(address_space_root_t root,
					   uintptr_t start, size_t length)
{
	uintptr_t check;

	for (check = start; check < start + length; check += PAGE_SIZE_4KB)
	{
		if (is_page_mapped_in_directory(root, check, NULL) == 1)
			return check;
	}
	return 0;
}

/* Lowest start of a range [base, base + size) that overlaps the window. */
static void mm_va_note_blocker(uintptr_t *low, uintptr_t start, size_t length,
			       uintptr_t base, size_t size)
{
	if (size && base < start + length && start < base + size &&
	    (*low == 0 || base < *low))
		*low = base;
}

/*
 * Lowest start of the heap, stack or VMA overlapping [start, start +
 * length), or 0 when none does (the ranges process_user_va_range_overlaps()
 * checks).
 */
static uintptr_t mm_va_range_lowest_region(process_t *proc, uintptr_t start,
					   size_t length)
{
	struct mmap_region *r;
	uintptr_t low = 0;

	if (process_heap_end(proc) > process_heap_start(proc))
		mm_va_note_blocker(&low, start, length,
				   (uintptr_t)process_heap_start(proc),
				   (size_t)(process_heap_end(proc) -
					    process_heap_start(proc)));
	mm_va_note_blocker(&low, start, length,
			   (uintptr_t)process_stack_start(proc),
			   (size_t)process_stack_size(proc));
	for (r = process_mmap_list(proc); r; r = r->next)
		mm_va_note_blocker(&low, start, length, (uintptr_t)r->addr,
				   r->length);
	return low;
}

static uintptr_t mm_mmap_search_low(process_t *proc)
{
	uintptr_t search_low = USER_MMAP_START;

	if (!proc)
		return search_low;

	if (process_heap_end(proc) > process_heap_start(proc) &&
	    process_heap_end(proc) > search_low)
	{
		search_low = (uintptr_t)(process_heap_end(proc) + PAGE_SIZE_4KB - 1) &
			     ~(PAGE_SIZE_4KB - 1);
	}
	return search_low;
}

static uintptr_t mm_mmap_stack_ref(process_t *proc)
{
	if (!proc || proc->mode != USER_MODE || !process_stack_start(proc))
		return USER_STACK_BASE;

	return (uintptr_t)process_stack_start(proc);
}

static uintptr_t mm_mmap_search_end(process_t *proc)
{
	if (!proc)
		return USER_MMAP_END;

	return ir0_mmap_stack_search_end(mm_mmap_stack_ref(proc));
}

uintptr_t mm_pick_free_va_topdown(process_t *proc, address_space_root_t root,
					 size_t length)
{
	uintptr_t search_end;
	uintptr_t search_low;
	uintptr_t top;
	uintptr_t start;

	if (!proc || !root || length == 0)
		return 0;

	if (length > (size_t)(USER_MMAP_END - USER_MMAP_START))
		return 0;

	{
		uintptr_t stack_ref = mm_mmap_stack_ref(proc);

		search_low = mm_mmap_search_low(proc);
		search_end = mm_mmap_search_end(proc);
		if (search_low >= search_end || length > search_end - search_low)
			return 0;

		top = (uintptr_t)process_mmap_base(proc);
		if (top == 0 || top > search_end)
			top = search_end;
		/*
		 * Linux vm_unmapped_area() top-down: a window that hits a
		 * mapping moves to end at that mapping's lowest start, so the
		 * search takes one step per mapping in the way.
		 */
		start = (top - length) & ~(uintptr_t)(PAGE_SIZE_4KB - 1);
		while (start >= search_low)
		{
			uintptr_t low;

			if (!ir0_mmap_respects_stack_gap(start, length, stack_ref))
			{
				if (start < search_low + PAGE_SIZE_4KB)
					break;
				start -= PAGE_SIZE_4KB;
				continue;
			}
			low = mm_va_range_lowest_region(proc, start, length);
			if (!low)
				low = mm_va_range_lowest_mapped(root, start, length);
			if (!low)
			{
				process_set_mmap_base(proc, start);
				return start;
			}
			if (low < search_low + length)
				break;
			start = (low - length) & ~(uintptr_t)(PAGE_SIZE_4KB - 1);
		}
	}
	return 0;
}

uintptr_t mm_find_free_va(address_space_root_t root, process_t *proc,
			  uintptr_t hint,
			  size_t length)
{
	if (hint != 0)
	{
		if ((hint & (PAGE_SIZE_4KB - 1)) != 0)
			return 0;
		if (hint < USER_MMAP_START ||
		    hint + length > USER_MMAP_END || hint + length < hint)
			return 0;
		if (!mm_user_range_mappable(hint, length))
			return 0;
		if (proc && process_user_va_range_overlaps(proc, hint, length))
			return 0;
		if (is_page_mapped_in_directory(root, hint, NULL) == 1)
			return 0;
		return hint;
	}

	return mm_pick_free_va_topdown(proc, root, length);
}
