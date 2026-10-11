/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mm.c
 * Description: Process MM: user unmap, address-space creation, VA overlap, mmap lifecycle.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <ir0/mm.h>

static bool process_va_ranges_overlap(uintptr_t a_start, size_t a_len,
				      uintptr_t b_start, size_t b_len)
{
	uintptr_t a_end;
	uintptr_t b_end;

	if (a_len == 0 || b_len == 0)
		return false;
	/* An unrepresentable half-open range must never become a free hole. */
	if (a_len > UINTPTR_MAX - a_start || b_len > UINTPTR_MAX - b_start)
		return true;

	a_end = a_start + a_len;
	b_end = b_start + b_len;
	return a_start < b_end && b_start < a_end;
}

bool process_user_va_range_overlaps(process_t *proc, uintptr_t addr, size_t length)
{
	struct mmap_region *r;

	if (!proc || length == 0)
		return false;

	if (process_heap_end(proc) > process_heap_start(proc) &&
	    process_va_ranges_overlap(addr, length, process_heap_start(proc),
				      (size_t)(process_heap_end(proc) - process_heap_start(proc))))
		return true;

	if (process_stack_size(proc) > 0 &&
	    process_va_ranges_overlap(addr, length, process_stack_start(proc),
				      (size_t)process_stack_size(proc)))
		return true;

	for (r = process_mmap_list(proc); r; r = r->next)
	{
		if (process_va_ranges_overlap(addr, length, (uintptr_t)r->addr,
					      r->length))
			return true;
	}

	return false;
}

uint64_t *process_pt_child(uint64_t *table, size_t index)
{
	if (!mm_pte_present(table[index]))
		return NULL;
	if (mm_pte_large(table[index]))
		return NULL;
	return (uint64_t *)mm_pte_phys(table[index]);
}

uint64_t mm_count_resident_user_pages(const mm_struct_t *mm)
{
	size_t i4;
	size_t i3;
	size_t i2;
	size_t i1;
	uint64_t count = 0;
	address_space_root_t root;
	size_t entries;

	if (!mm || !mm->page_directory)
		return 0;

	root = mm->page_directory;
	entries = mm_table_entries();
	if (entries == 0)
		return 0;
	for (i4 = 0; i4 < (size_t)mm_user_root_slots(); i4++)
	{
		uint64_t *level1 = process_pt_child(root, i4);

		if (!level1)
			continue;

		for (i3 = 0; i3 < entries; i3++)
		{
			uint64_t *level2 = process_pt_child(level1, i3);
			uint64_t level1_entry;

			if (!level2)
			{
				level1_entry = level1[i3];
				if (mm_pte_present(level1_entry) &&
				    (level1_entry & PAGE_USER) && mm_pte_large(level1_entry))
					count += mm_large_leaf_pages(1);
				continue;
			}

			for (i2 = 0; i2 < entries; i2++)
			{
				uint64_t level2_entry = level2[i2];
				uint64_t *level3;

				if (!mm_pte_present(level2_entry))
					continue;
				if ((level2_entry & PAGE_USER) && mm_pte_large(level2_entry))
				{
					count += mm_large_leaf_pages(2);
					continue;
				}

				level3 = process_pt_child(level2, i2);
				if (!level3)
					continue;

				for (i1 = 0; i1 < entries; i1++)
				{
					uint64_t ent = level3[i1];

					if (mm_pte_present(ent) && (ent & PAGE_USER))
						count++;
				}
			}
		}
	}

	return count;
}

uint64_t process_count_resident_user_pages(const process_t *p)
{
	if (!p || !p->mm)
		return 0;

	return mm_count_resident_user_pages(p->mm);
}

/*
 * Drop every present user mapping below the architecture's user-root slots so PMM
 * frames are returned and the address space can be discarded safely while
 * another process is active (its address-space root may be unrelated).
 */
void process_unmap_user_pages_all(address_space_root_t root,
					 process_reclaim_stats_t *stats)
{
	size_t i4;
	size_t i3;
	size_t i2;
	size_t i1;
	size_t entries;

	if (!root)
		return;
	entries = mm_table_entries();
	if (entries == 0)
		return;

	for (i4 = 0; i4 < (size_t)mm_user_root_slots(); i4++)
	{
		uint64_t *level1 = process_pt_child(root, i4);

		if (!level1)
			continue;
		if (stats)
			stats->level1_present++;

		for (i3 = 0; i3 < entries; i3++)
		{
			uint64_t *level2 = process_pt_child(level1, i3);

			if (!level2)
				continue;
			if (stats)
				stats->level2_present++;

			for (i2 = 0; i2 < entries; i2++)
			{
				uint64_t *level3 = process_pt_child(level2, i2);

				if (!level3)
					continue;
				if (stats)
					stats->level3_present++;

				for (i1 = 0; i1 < entries; i1++)
				{
					uint64_t ent = level3[i1];
					uintptr_t virt;
					size_t idx[4];

					if (!mm_pte_present(ent) || !(ent & PAGE_USER))
						continue;
					if (stats)
					{
						stats->mapped_pages++;
						stats->leaf_present++;
					}

					idx[0] = i4;
					idx[1] = i3;
					idx[2] = i2;
					idx[3] = i1;
					virt = mm_va_from_indices(idx);
					if (unmap_page_in_directory(root, virt) == 0)
					{
						if (stats)
						{
							stats->freed_pages++;
							stats->leaf_freed++;
						}
					}
					else if (stats)
					{
						stats->missing_pages++;
					}
				}
			}
		}
	}
}


void process_unmap_user_address_space(process_t *p)
{
	process_reclaim_stats_t stats;
	uint64_t orphan_frames = 0;
	uint64_t double_free = 0;
	uint64_t alive_owner_missing = 0;

	if (!p)
		return;
	memset(&stats, 0, sizeof(stats));

	process_unmap_user_pages_all(process_pgd(p), &stats);
	pmm_owner_audit(&orphan_frames, &double_free, &alive_owner_missing);
	(void)orphan_frames;
	(void)double_free;
	(void)alive_owner_missing;
}


struct mmap_region *process_clone_mmap_list(struct mmap_region *parent_list)
{
	struct mmap_region *head = NULL;
	struct mmap_region *tail = NULL;
	struct mmap_region *walk;

	for (walk = parent_list; walk; walk = walk->next)
	{
		struct mmap_region *node = kmalloc_try(sizeof(*node));

		if (!node)
		{
			while (head)
			{
				struct mmap_region *next = head->next;

				kfree(head);
				head = next;
			}
			return NULL;
		}

		memcpy(node, walk, sizeof(*node));
		node->next = NULL;

		if (!head)
			head = node;
		else
			tail->next = node;
		tail = node;
	}

	return head;
}

void process_fork_destroy_child_mm(process_t *child)
{
	if (!child)
		return;

	if (child->mm)
	{
		mm_put(child->mm);
		child->mm = NULL;
	}
}

void process_fork_free_mmap_list(process_t *child)
{
	struct mmap_region *r;
	struct mmap_region *next;

	if (!child)
		return;

	r = process_mmap_list(child);
	while (r)
	{
		next = r->next;
		kfree(r);
		r = next;
	}
	process_mm_set_mmap_list(child, NULL);
}

uint64_t create_process_page_directory(void)
{
	address_space_root_t root = mm_create_user_root();

	return (uint64_t)(uintptr_t)root;
}
