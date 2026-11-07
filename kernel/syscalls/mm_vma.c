/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mm_vma.c
 * Description: Per-process VMA list: bounds, merge, range detach, protection change and MAP_FIXED preparation.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <kernel/syscalls.h>
#include "mm_syscalls.h"
#include "syscalls_glue.h"
#include <ir0/abi/mmap_contract.h>
#include <config.h>
#include <ir0/vfs.h>
#include <ir0/copy_user.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/ktm/klog.h>
#include <ir0/stat.h>
#include <ir0/process.h>
#include <ir0/files_struct.h>
#include <ir0/mm_struct.h>
#include <ir0/paging.h>
#include <ir0/pmm.h>
#include <ir0/mm_port.h>
#include <ir0/mman.h>
#include <ir0/clock.h>
#include <mm/allocator.h>
#include <ir0/arch_port.h>
#include <ir0/arch_mm.h>
#include <ir0/ktm/checkpoint.h>
#include <ir0/swap.h>
#include <ir0/path_user.h>
#include <ir0/permissions.h>
#include <ir0/blockdev.h>
#include <ir0/vfs_dispatch.h>
#include <stdbool.h>
#include <string.h>

#include <ir0/fb.h>
#include <ir0/memfd.h>
#include <ir0/validation.h>
#include <stdint.h>
#include "mm_syscalls_internal.h"

/*
 * Compute the page-aligned bounds of a VMA. Returns -1 when the stored
 * length cannot be aligned without wrapping.
 */
static int mm_vma_bounds(const struct mmap_region *region, uintptr_t *start,
			 uintptr_t *end)
{
	uintptr_t region_start;
	size_t aligned_length;

	if (!region || !start || !end)
		return -1;
	if (region->length > UINTPTR_MAX - (PAGE_SIZE_4KB - 1))
		return -1;
	region_start = (uintptr_t)region->addr & PAGE_FRAME_MASK;
	aligned_length = (region->length + PAGE_SIZE_4KB - 1) & PAGE_FRAME_MASK;
	if (region_start > UINTPTR_MAX - aligned_length)
		return -1;
	*start = region_start;
	*end = region_start + aligned_length;
	return 0;
}

/*
 * Coalesce contiguous VMAs that share prot and flags.  The list is built by
 * prepending, so two address-adjacent regions need not be list-adjacent;
 * scan the whole list until no merge is possible.  O(n^2) is fine here: the
 * list only tracks sys_mmap regions and stays short.
 */
static void mm_merge_adjacent_vmas(void)
{
	struct mmap_region **head = process_mmap_list_p(current_process);
	struct mmap_region *a;
	int merged;

	if (!head)
		return;

	do {
		merged = 0;
		for (a = *head; a && !merged; a = a->next)
		{
			struct mmap_region **link = &a->next;
			struct mmap_region *b;
			uintptr_t a_start;
			uintptr_t a_end;

			if (mm_vma_bounds(a, &a_start, &a_end) != 0)
				continue;
			for (b = a->next; b; b = b->next)
			{
				uintptr_t b_start;
				uintptr_t b_end;
				uintptr_t start;
				uintptr_t end;

				if (b->prot != a->prot || b->flags != a->flags ||
				    mm_vma_bounds(b, &b_start, &b_end) != 0)
				{
					link = &b->next;
					continue;
				}
				if (a_end != b_start && b_end != a_start)
				{
					link = &b->next;
					continue;
				}
				start = a_start < b_start ? a_start : b_start;
				end = a_end > b_end ? a_end : b_end;
				a->addr = (void *)start;
				a->length = end - start;
				*link = b->next;
				kfree(b);
				merged = 1;
				break;
			}
		}
	} while (merged);
}

/*
 * Detach [start, end) from the VMA list, splitting a region when the range
 * covers only its middle. munmap(2) and MAP_FIXED both replace part of a
 * mapping; dropping the whole VMA there left the remaining PTEs untracked
 * and leaked their frames.
 *
 * A single contiguous range can split at most one VMA, so one spare node is
 * allocated up front. On -ENOMEM the list is untouched.
 *
 * Returns 1 when at least one VMA overlapped, 0 when none did, or -ENOMEM.
 */
int mm_detach_vma_range(uintptr_t start, uintptr_t end)
{
	struct mmap_region **link;
	struct mmap_region *spare = NULL;
	struct mmap_region *walk;
	int need_split = 0;
	int touched = 0;

	if (!current_process || start >= end)
		return 0;
	link = process_mmap_list_p(current_process);
	if (!link)
		return 0;

	for (walk = *link; walk; walk = walk->next)
	{
		uintptr_t region_start;
		uintptr_t region_end;

		if (mm_vma_bounds(walk, &region_start, &region_end) != 0)
			continue;
		if (region_end <= start || region_start >= end)
			continue;
		if (region_start < start && region_end > end)
		{
			need_split = 1;
			break;
		}
	}
	if (need_split)
	{
		spare = kmalloc_try(sizeof(*spare));
		if (!spare)
			return -ENOMEM;
	}

	link = process_mmap_list_p(current_process);
	while (*link)
	{
		struct mmap_region *region = *link;
		uintptr_t region_start;
		uintptr_t region_end;

		if (mm_vma_bounds(region, &region_start, &region_end) != 0 ||
		    region_end <= start || region_start >= end)
		{
			link = &region->next;
			continue;
		}

		touched = 1;
		if (start <= region_start && end >= region_end)
		{
			*link = region->next;
			kfree(region);
			continue;
		}

		if (region_start < start && end < region_end)
		{
			struct mmap_region *right = spare;

			spare = NULL;
			right->addr = (void *)end;
			right->hint_addr = (void *)end;
			right->length = region_end - end;
			right->prot = region->prot;
			right->flags = region->flags;
			right->next = region->next;
			region->length = start - region_start;
			region->next = right;
			link = &right->next;
			continue;
		}

		if (region_start < start)
		{
			/* Keep [region_start, start), drop the tail. */
			region->length = start - region_start;
			link = &region->next;
			continue;
		}

		/* Keep [end, region_end), drop the head. */
		region->addr = (void *)end;
		region->hint_addr = (void *)end;
		region->length = region_end - end;
		link = &region->next;
	}

	if (spare)
		kfree(spare);
	return touched;
}

/*
 * Apply a protection change to the VMA list without dropping ranges.
 * mprotect(2) may cover only part of a region; Linux splits the VMA so
 * /proc/pid/maps reports the permissions that actually apply to each range.
 * The PTEs are updated separately by the caller.  All split nodes are
 * preallocated so a failed mprotect cannot leave a half-split list.
 *
 * Returns 0 on success or -ENOMEM when the split nodes cannot be allocated.
 */
int mm_apply_vma_prot(uintptr_t start, uintptr_t end, int prot)
{
	struct mmap_region **link;
	struct mmap_region *walk;
	struct mmap_region *spares = NULL;
	int need = 0;
	int touched = 0;

	if (!current_process || start >= end)
		return 0;
	link = process_mmap_list_p(current_process);
	if (!link)
		return 0;

	for (walk = *link; walk; walk = walk->next)
	{
		uintptr_t region_start;
		uintptr_t region_end;

		if (mm_vma_bounds(walk, &region_start, &region_end) != 0)
			continue;
		if (region_end <= start || region_start >= end)
			continue;
		if (region_start < start && end < region_end)
			need += 2;
		else if (region_start < start || end < region_end)
			need += 1;
	}

	while (need > 0)
	{
		struct mmap_region *node = kmalloc_try(sizeof(*node));

		if (!node)
		{
			while (spares)
			{
				struct mmap_region *next = spares->next;

				kfree(spares);
				spares = next;
			}
			return -ENOMEM;
		}
		node->next = spares;
		spares = node;
		need--;
	}

	link = process_mmap_list_p(current_process);
	while (*link)
	{
		struct mmap_region *region = *link;
		uintptr_t region_start;
		uintptr_t region_end;
		int old_prot;

		if (mm_vma_bounds(region, &region_start, &region_end) != 0 ||
		    region_end <= start || region_start >= end)
		{
			link = &region->next;
			continue;
		}

		old_prot = region->prot;
		if (start <= region_start && end >= region_end)
		{
			region->prot = prot;
			touched = 1;
			link = &region->next;
			continue;
		}

		if (region_start < start && end < region_end)
		{
			struct mmap_region *middle = spares;
			struct mmap_region *right;

			spares = middle->next;
			right = spares;
			spares = right->next;

			middle->addr = (void *)start;
			middle->hint_addr = (void *)start;
			middle->length = end - start;
			middle->prot = prot;
			middle->flags = region->flags;
			middle->next = right;

			right->addr = (void *)end;
			right->hint_addr = (void *)end;
			right->length = region_end - end;
			right->prot = old_prot;
			right->flags = region->flags;
			right->next = region->next;

			region->length = start - region_start;
			region->next = middle;
			touched = 1;
			link = &right->next;
			continue;
		}

		if (region_start < start)
		{
			/* Keep [region_start, start) old, add [start, region_end) new. */
			struct mmap_region *right = spares;

			spares = right->next;
			right->addr = (void *)start;
			right->hint_addr = (void *)start;
			right->length = region_end - start;
			right->prot = prot;
			right->flags = region->flags;
			right->next = region->next;

			region->length = start - region_start;
			region->next = right;
			touched = 1;
			link = &right->next;
			continue;
		}

		/* start <= region_start && end < region_end: [region_start, end) new. */
		{
			struct mmap_region *left = spares;

			spares = left->next;
			left->addr = (void *)region_start;
			left->hint_addr = (void *)region_start;
			left->length = end - region_start;
			left->prot = prot;
			left->flags = region->flags;
			left->next = region;

			region->addr = (void *)end;
			region->hint_addr = (void *)end;
			region->length = region_end - end;
			*link = left;
			touched = 1;
			link = &region->next;
			continue;
		}
	}

	while (spares)
	{
		struct mmap_region *next = spares->next;

		kfree(spares);
		spares = next;
	}

	/*
	 * mprotect splits a region to track per-range permissions.  Restoring
	 * the original protection must coalesce the pieces again: otherwise
	 * every split/restore cycle adds VMAs and /proc/pid/maps grows a line
	 * per fragment.  Linux merges adjacent VMAs with identical flags.
	 */
	if (touched)
		mm_merge_adjacent_vmas();
	return 0;
}

int mm_prepare_map_fixed(uintptr_t start, size_t length)
{
	uintptr_t end;
	int detached;
	int failed;

	if (!current_process || length == 0)
		return 0;

	if (start > UINTPTR_MAX - length)
		return -EINVAL;
	end = start + length;
	detached = mm_detach_vma_range(start, end);
	if (detached < 0)
		return detached;

	failed = 0;
	for (uintptr_t page = start; page < end; page += PAGE_SIZE_4KB)
	{
		if (is_page_mapped_in_directory(process_pgd(current_process), page,
						NULL) == 1 &&
		    unmap_page_in_directory(process_pgd(current_process),
					    page) != 0)
			failed = 1;
	}
	if (failed)
		return -ENOMEM;
	return 0;
}
