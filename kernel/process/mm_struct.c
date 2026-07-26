/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mm_struct.c
 * Description: mm_struct refcount and process bind/share helpers.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <ir0/mm_struct.h>
#include <ir0/errno.h>
#include <ir0/paging.h>
#include <string.h>

mm_struct_t *mm_create(void)
{
	mm_struct_t *mm;

	mm = kmalloc_try(sizeof(*mm));
	if (!mm)
		return NULL;

	memset(mm, 0, sizeof(*mm));
	mm->refcount = 1;
	return mm;
}

mm_struct_t *mm_get(mm_struct_t *mm)
{
	if (!mm)
		return NULL;
	mm->refcount++;
	return mm;
}

void mm_put(mm_struct_t *mm)
{
	if (!mm)
		return;

	if (mm->refcount <= 0)
	{
		panic("mm_put: refcount underflow");
		return;
	}

	mm->refcount--;
	if (mm->refcount > 0)
		return;

	if (mm->page_directory && mm->owns_tables)
	{
		process_unmap_user_pages_all(mm->page_directory, NULL);
		paging_reclaim_lower_half_tables(mm->page_directory);
		paging_ir0_mm_note_pml4_freed((uint64_t)(uintptr_t)mm->page_directory);
		kfree_aligned(mm->page_directory);
		mm->page_directory = NULL;
	}

	{
		struct mmap_region *r = mm->mmap_list;
		struct mmap_region *next;

		while (r)
		{
			next = r->next;
			kfree(r);
			r = next;
		}
		mm->mmap_list = NULL;
	}
	kfree(mm);
}

void process_mm_bind(process_t *p, mm_struct_t *mm)
{
	if (!p)
		return;

	p->mm = mm;
	if (!mm)
	{
		p->page_directory = NULL;
		p->owns_page_directory = 0;
		p->mmap_list = NULL;
		return;
	}

	p->page_directory = mm->page_directory;
	/*
	 * Mirror ownership: only the sole user with owns_tables tears down via
	 * legacy paths; shared mm always clears the process-local owns flag.
	 */
	p->owns_page_directory = (mm->owns_tables && mm->refcount == 1) ? 1 : 0;
	p->mmap_list = mm->mmap_list;
	p->mmap_base = mm->mmap_base;
	p->heap_start = mm->heap_start;
	p->heap_end = mm->heap_end;
	p->stack_start = mm->stack_start;
	p->stack_size = mm->stack_size;
}

int process_mm_share(process_t *child, process_t *parent)
{
	mm_struct_t *mm;

	if (!child || !parent)
		return -EINVAL;

	mm = parent->mm;
	if (!mm)
	{
		/*
		 * Legacy parent without mm object: wrap existing page tables.
		 */
		mm = mm_create();
		if (!mm)
			return -ENOMEM;
		mm->page_directory = process_pgd(parent);
		mm->owns_tables = process_mm_owns_tables(parent) ? 1 : 0;
		mm->mmap_list = parent->mmap_list;
		mm->mmap_base = parent->mmap_base;
		mm->heap_start = parent->heap_start;
		mm->heap_end = parent->heap_end;
		mm->stack_start = parent->stack_start;
		mm->stack_size = parent->stack_size;
		process_mm_bind(parent, mm);
	}

	(void)mm_get(mm);
	process_mm_bind(child, mm);
	return 0;
}

void process_mm_sync_to_process(process_t *p)
{
	mm_struct_t *mm;

	if (!p)
		return;
	mm = p->mm;
	if (!mm)
		return;

	p->page_directory = mm->page_directory;
	p->owns_page_directory =
		(mm->owns_tables && mm->refcount == 1) ? 1 : 0;
	p->mmap_list = mm->mmap_list;
	p->mmap_base = mm->mmap_base;
	p->heap_start = mm->heap_start;
	p->heap_end = mm->heap_end;
	p->stack_start = mm->stack_start;
	p->stack_size = mm->stack_size;
}

void process_mm_sync_from_process(process_t *p)
{
	mm_struct_t *mm;

	if (!p)
		return;
	mm = p->mm;
	if (!mm)
		return;

	/*
	 * mm->page_directory is canonical; process_set_pgd() already keeps the
	 * process mirror aligned. Only sync VMA/heap/stack cursors here.
	 */
	mm->mmap_list = p->mmap_list;
	mm->mmap_base = p->mmap_base;
	mm->heap_start = p->heap_start;
	mm->heap_end = p->heap_end;
	mm->stack_start = p->stack_start;
	mm->stack_size = p->stack_size;
}

void process_mm_set_mmap_list(process_t *p, struct mmap_region *list)
{
	if (!p)
		return;

	p->mmap_list = list;
	if (p->mm)
		p->mm->mmap_list = list;
}

int process_mm_owns_tables(const process_t *p)
{
	mm_struct_t *mm;

	if (!p)
		return 0;
	mm = p->mm;
	if (mm)
		return (mm->owns_tables && mm->refcount == 1) ? 1 : 0;
	return p->owns_page_directory ? 1 : 0;
}
