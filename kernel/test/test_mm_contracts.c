/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_mm_contracts.c
 * Description: ktest — munmap/mprotect VMA and COW invariants
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test/ktest_harness.h"
#include "syscalls.h"
#include <ir0/abi/mmap_contract.h>
#include <ir0/kmem.h>
#include <ir0/paging.h>
#include <ir0/process.h>
#include <stdint.h>

#define KTEST_MAP_PRIVATE   0x02
#define KTEST_MAP_ANONYMOUS 0x20
#define KTEST_MAP_FIXED     0x10
#define KTEST_PROT_READ     0x1
#define KTEST_PROT_WRITE    0x2
#define KTEST_PROT_RW       (KTEST_PROT_READ | KTEST_PROT_WRITE)

#define KTEST_BUSYBOX_INITIAL_BRK 0x453000UL

static int ktest_mm_pte_flags(uintptr_t va, uint64_t *flags)
{
	if (!current_process || !process_pgd(current_process))
		return 0;
	return is_page_mapped_in_directory(process_pgd(current_process), va,
					   flags) == 1;
}

static int ktest_mm_present(uintptr_t va)
{
	return ktest_mm_pte_flags(va, NULL);
}

static void *ktest_mm_mmap_anon(size_t len, int prot, int flags)
{
	return sys_mmap(NULL, len, prot, flags, -1, 0);
}

/*
 * A failed test must not leave a stale VMA behind for the next test. This is
 * the same "drop any overlapping region" rule MAP_FIXED uses; the pages are
 * unmapped separately below.
 */
static void ktest_mm_drop_vmas(uintptr_t start, uintptr_t end)
{
	struct mmap_region **link = process_mmap_list_p(current_process);

	if (!link)
		return;
	while (*link)
	{
		struct mmap_region *region = *link;
		uintptr_t region_start = (uintptr_t)region->addr & PAGE_FRAME_MASK;
		uintptr_t region_end = region_start +
			((region->length + PAGE_SIZE_4KB - 1) & PAGE_FRAME_MASK);

		if (region_end <= start || region_start >= end)
		{
			link = &region->next;
			continue;
		}
		*link = region->next;
		kfree(region);
	}
}

static void ktest_mm_cleanup(uintptr_t base, size_t len)
{
	address_space_root_t root = process_pgd(current_process);
	size_t off;

	for (off = 0; off < len; off += PAGE_SIZE_4KB)
	{
		if (ktest_mm_present(base + off))
			(void)unmap_page_in_directory(root, base + off);
	}
	ktest_mm_drop_vmas(base, base + len);
}

void ktest_munmap_partial_preserves_vma(void)
{
	void *region;
	uintptr_t base;
	uint64_t saved_heap_start;
	uint64_t saved_heap_end;
	uint64_t saved_mmap_base;
	size_t i;
	int rc;

	KTEST_BEGIN("munmap_partial_preserves_vma");
	KASSERT(current_process != NULL);

	saved_heap_start = process_heap_start(current_process);
	saved_heap_end = process_heap_end(current_process);
	saved_mmap_base = process_mmap_base(current_process);
	process_set_heap_start(current_process, KTEST_BUSYBOX_INITIAL_BRK);
	process_set_heap_end(current_process, KTEST_BUSYBOX_INITIAL_BRK);
	process_set_mmap_base(current_process, 0);

	region = ktest_mm_mmap_anon(4 * PAGE_SIZE_4KB, KTEST_PROT_RW,
				    KTEST_MAP_PRIVATE | KTEST_MAP_ANONYMOUS);
	KASSERT(region != NULL && region != (void *)(intptr_t)-1);
	if (region == NULL || region == (void *)(intptr_t)-1)
	{
		process_set_heap_start(current_process, saved_heap_start);
		process_set_heap_end(current_process, saved_heap_end);
		process_set_mmap_base(current_process, saved_mmap_base);
		KTEST_END();
		return;
	}
	base = (uintptr_t)region;

	for (i = 0; i < 4; i++)
		KASSERT(ktest_mm_present(base + i * PAGE_SIZE_4KB));

	/* Drop one page in the middle: the two remaining ranges must survive. */
	rc = sys_munmap((void *)(base + PAGE_SIZE_4KB), PAGE_SIZE_4KB);
	KASSERT_EQ(rc, 0);
	KASSERT(!ktest_mm_present(base + PAGE_SIZE_4KB));
	KASSERT(ktest_mm_present(base));
	KASSERT(ktest_mm_present(base + 2 * PAGE_SIZE_4KB));
	KASSERT(ktest_mm_present(base + 3 * PAGE_SIZE_4KB));

	/* The low range is still tracked, so munmap must find it. */
	rc = sys_munmap((void *)base, PAGE_SIZE_4KB);
	KASSERT_EQ(rc, 0);
	KASSERT(!ktest_mm_present(base));

	rc = sys_munmap((void *)(base + 2 * PAGE_SIZE_4KB),
			2 * PAGE_SIZE_4KB);
	KASSERT_EQ(rc, 0);
	KASSERT(!ktest_mm_present(base + 2 * PAGE_SIZE_4KB));
	KASSERT(!ktest_mm_present(base + 3 * PAGE_SIZE_4KB));

	ktest_mm_cleanup(base, 4 * PAGE_SIZE_4KB);

	process_set_heap_start(current_process, saved_heap_start);
	process_set_heap_end(current_process, saved_heap_end);
	process_set_mmap_base(current_process, saved_mmap_base);
	KTEST_END();
}

void ktest_mmap_fixed_partial_overlap_preserves_vma(void)
{
	void *region;
	void *fixed;
	uintptr_t base;
	uint64_t saved_heap_start;
	uint64_t saved_heap_end;
	uint64_t saved_mmap_base;
	size_t i;
	int rc;

	KTEST_BEGIN("mmap_fixed_partial_overlap_preserves_vma");
	KASSERT(current_process != NULL);

	saved_heap_start = process_heap_start(current_process);
	saved_heap_end = process_heap_end(current_process);
	saved_mmap_base = process_mmap_base(current_process);
	process_set_heap_start(current_process, KTEST_BUSYBOX_INITIAL_BRK);
	process_set_heap_end(current_process, KTEST_BUSYBOX_INITIAL_BRK);
	process_set_mmap_base(current_process, 0);

	region = ktest_mm_mmap_anon(4 * PAGE_SIZE_4KB, KTEST_PROT_RW,
				    KTEST_MAP_PRIVATE | KTEST_MAP_ANONYMOUS);
	KASSERT(region != NULL && region != (void *)(intptr_t)-1);
	if (region == NULL || region == (void *)(intptr_t)-1)
	{
		process_set_heap_start(current_process, saved_heap_start);
		process_set_heap_end(current_process, saved_heap_end);
		process_set_mmap_base(current_process, saved_mmap_base);
		KTEST_END();
		return;
	}
	base = (uintptr_t)region;

	/*
	 * Replace the two middle pages. The first and last pages must stay
	 * tracked as VMAs even though MAP_FIXED only covers the middle.
	 */
	fixed = sys_mmap((void *)(base + PAGE_SIZE_4KB), 2 * PAGE_SIZE_4KB,
			 KTEST_PROT_RW,
			 KTEST_MAP_PRIVATE | KTEST_MAP_ANONYMOUS |
				 KTEST_MAP_FIXED,
			 -1, 0);
	KASSERT_EQ((uintptr_t)fixed, base + PAGE_SIZE_4KB);
	KASSERT(ktest_mm_present(base));
	KASSERT(ktest_mm_present(base + PAGE_SIZE_4KB));
	KASSERT(ktest_mm_present(base + 2 * PAGE_SIZE_4KB));
	KASSERT(ktest_mm_present(base + 3 * PAGE_SIZE_4KB));

	rc = sys_munmap((void *)base, PAGE_SIZE_4KB);
	KASSERT_EQ(rc, 0);
	KASSERT(!ktest_mm_present(base));

	rc = sys_munmap((void *)(base + 3 * PAGE_SIZE_4KB), PAGE_SIZE_4KB);
	KASSERT_EQ(rc, 0);
	KASSERT(!ktest_mm_present(base + 3 * PAGE_SIZE_4KB));

	/* Drop the replacement range and anything a failure left behind. */
	(void)sys_munmap((void *)(base + PAGE_SIZE_4KB), 2 * PAGE_SIZE_4KB);
	for (i = 0; i < 4; i++)
	{
		if (ktest_mm_present(base + i * PAGE_SIZE_4KB))
			(void)sys_munmap((void *)(base + i * PAGE_SIZE_4KB),
					 PAGE_SIZE_4KB);
	}
	ktest_mm_cleanup(base, 4 * PAGE_SIZE_4KB);

	process_set_heap_start(current_process, saved_heap_start);
	process_set_heap_end(current_process, saved_heap_end);
	process_set_mmap_base(current_process, saved_mmap_base);
	KTEST_END();
}

void ktest_mprotect_preserves_cow(void)
{
	void *region;
	uintptr_t base;
	uint64_t *pte;
	uint64_t flags = 0;
	uint64_t saved_heap_start;
	uint64_t saved_heap_end;
	uint64_t saved_mmap_base;
	int rc;

	KTEST_BEGIN("mprotect_preserves_cow");
	KASSERT(current_process != NULL);

	saved_heap_start = process_heap_start(current_process);
	saved_heap_end = process_heap_end(current_process);
	saved_mmap_base = process_mmap_base(current_process);
	process_set_heap_start(current_process, KTEST_BUSYBOX_INITIAL_BRK);
	process_set_heap_end(current_process, KTEST_BUSYBOX_INITIAL_BRK);
	process_set_mmap_base(current_process, 0);

	region = ktest_mm_mmap_anon(PAGE_SIZE_4KB, KTEST_PROT_RW,
				    KTEST_MAP_PRIVATE | KTEST_MAP_ANONYMOUS);
	KASSERT(region != NULL && region != (void *)(intptr_t)-1);
	if (region == NULL || region == (void *)(intptr_t)-1)
	{
		process_set_heap_start(current_process, saved_heap_start);
		process_set_heap_end(current_process, saved_heap_end);
		process_set_mmap_base(current_process, saved_mmap_base);
		KTEST_END();
		return;
	}
	base = (uintptr_t)region;

	pte = paging_get_pte(process_pgd(current_process), base);
	KASSERT(pte != NULL);
	if (pte)
	{
		/* Simulate a fork-shared page: read-only plus the COW bit. */
		*pte = (*pte & ~PAGE_RW) | PAGE_COW;
		KASSERT(ktest_mm_pte_flags(base, &flags));
		KASSERT((flags & PAGE_COW) != 0);
		KASSERT((flags & PAGE_RW) == 0);

		rc = sys_mprotect(region, PAGE_SIZE_4KB, KTEST_PROT_RW);
		KASSERT_EQ(rc, 0);

		flags = 0;
		KASSERT(ktest_mm_pte_flags(base, &flags));
		/*
		 * A COW page must stay hardware read-only until its write
		 * fault copies the frame. Dropping PAGE_COW here would let
		 * the child write through the parent's frame.
		 */
		KASSERT((flags & PAGE_COW) != 0);
		KASSERT((flags & PAGE_RW) == 0);
	}

	KASSERT_EQ(sys_munmap(region, PAGE_SIZE_4KB), 0);
	ktest_mm_cleanup(base, PAGE_SIZE_4KB);

	process_set_heap_start(current_process, saved_heap_start);
	process_set_heap_end(current_process, saved_heap_end);
	process_set_mmap_base(current_process, saved_mmap_base);
	KTEST_END();
}
