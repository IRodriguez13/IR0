/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_mmap_null_placement.c
 * Description: ktest — Linux-like mmap(NULL) top-down placement
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test/ktest_harness.h"
#include "syscalls.h"
#include <config.h>
#include <ir0/abi/mmap_contract.h>
#include <ir0/copy_user.h>
#include <ir0/paging.h>
#include <ir0/process.h>
#include <stdint.h>

#define KTEST_MAP_PRIVATE   0x02
#define KTEST_MAP_ANONYMOUS 0x20
#define KTEST_MAP_FIXED     0x10
#define KTEST_PROT_RW       0x3

#define KTEST_BUSYBOX_INITIAL_BRK 0x453000UL

static int ktest_pte_state(uintptr_t va, uint64_t *flags)
{
	if (!current_process || !process_pgd(current_process))
		return 0;
	return is_page_mapped_in_directory(process_pgd(current_process), va,
					   flags) == 1;
}

static int ktest_pte_present(uintptr_t va)
{
	uint64_t flags = 0;

	return ktest_pte_state(va, &flags);
}

static int ktest_pte_user(uintptr_t va)
{
	uint64_t flags = 0;

	if (!ktest_pte_state(va, &flags))
		return 0;
	return (flags & PAGE_USER) != 0;
}

static void *ktest_mmap_anon(size_t len, int prot, int flags, void *addr)
{
	return sys_mmap(addr, len, prot, flags, -1, 0);
}

static void ktest_unmap_all(void **maps, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
	{
		if (maps[i] && maps[i] != (void *)(intptr_t)-1)
			sys_munmap(maps[i], 0x1000);
	}
}

/*
 * Pick a free page below the top-down allocations. Searching upward from
 * IR0_MMAP_NULL_MIN_VA was wrong: 0x60000000 sits inside the 1 GiB identity
 * leaf, which map_page_in_directory() cannot split. Stay in the high arena
 * where the tables are already 4 KiB-granular, and check both the VMA list
 * and the PTE so a MAP_FIXED does not silently replace another slot.
 */
static uintptr_t ktest_find_free_page_below(uintptr_t high)
{
	uintptr_t va = high & ~(PAGE_SIZE_4KB - 1);

	while (va >= USER_MMAP_START)
	{
		if (va != USER_MMAP_START &&
		    !process_user_va_range_overlaps(current_process, va,
						    PAGE_SIZE_4KB) &&
		    !ktest_pte_present(va) &&
		    ir0_mmap_respects_stack_gap(va, PAGE_SIZE_4KB,
						USER_STACK_BASE))
			return va;
		if (va < PAGE_SIZE_4KB)
			break;
		va -= PAGE_SIZE_4KB;
	}
	return 0;
}

void ktest_mmap_null_placement(void)
{
	void *slot[4];
	void *fixed = NULL;
	uintptr_t addrs[4];
	uint64_t saved_heap_start;
	uint64_t saved_heap_end;
	uint64_t saved_mmap_base;
	uintptr_t fixed_addr;
	size_t i;

	KTEST_BEGIN("mmap_null_placement");

	saved_heap_start = process_heap_start(current_process);
	saved_heap_end = process_heap_end(current_process);
	saved_mmap_base = process_mmap_base(current_process);

	process_set_heap_start(current_process, KTEST_BUSYBOX_INITIAL_BRK);
	process_set_heap_end(current_process, KTEST_BUSYBOX_INITIAL_BRK);
	process_set_mmap_base(current_process, 0);

	for (i = 0; i < 4; i++)
	{
		slot[i] = ktest_mmap_anon(0x1000, KTEST_PROT_RW,
					  KTEST_MAP_PRIVATE | KTEST_MAP_ANONYMOUS,
					  NULL);
		KASSERT(slot[i] != (void *)(intptr_t)-1);
		addrs[i] = (uintptr_t)slot[i];
		KASSERT(addrs[i] >= IR0_MMAP_NULL_MIN_VA);
		KASSERT(addrs[i] != USER_MMAP_START);
		KASSERT(ir0_mmap_respects_stack_gap(addrs[i], 0x1000,
						    USER_STACK_BASE));
		KASSERT(ktest_pte_present(addrs[i]));
		KASSERT(ktest_pte_user(addrs[i]));
	}

	for (i = 0; i < 4; i++)
	{
		size_t j;

		for (j = i + 1; j < 4; j++)
			KASSERT(addrs[i] != addrs[j]);
	}

	for (i = 0; i < 4; i++)
	{
		size_t j;

		for (j = i + 1; j < 4; j++)
			KASSERT(addrs[i] + 0x1000 <= addrs[j] ||
				addrs[j] + 0x1000 <= addrs[i]);
	}

	{
		uintptr_t lowest = addrs[0];

		for (i = 1; i < 4; i++)
			if (addrs[i] < lowest)
				lowest = addrs[i];
		fixed_addr = ktest_find_free_page_below(lowest - PAGE_SIZE_4KB);
	}
	KASSERT(fixed_addr != 0);
	if (fixed_addr != 0)
	{
		fixed = ktest_mmap_anon(0x1000, KTEST_PROT_RW,
					KTEST_MAP_PRIVATE | KTEST_MAP_ANONYMOUS |
						KTEST_MAP_FIXED,
					(void *)fixed_addr);
		KASSERT(fixed == (void *)fixed_addr);
		if (fixed == (void *)fixed_addr)
		{
			uint8_t written = 0x5A;
			uint8_t readback = 0;

			KASSERT(ktest_pte_present(fixed_addr));
			KASSERT(ktest_pte_user(fixed_addr));
			/* The mapping must be usable, not merely present in the PTE. */
			KASSERT(copy_to_user_mm(process_pgd(current_process),
						fixed_addr, &written,
						sizeof(written)) == 0);
			KASSERT(copy_from_user_mm(process_pgd(current_process),
						  fixed_addr, &readback,
						  sizeof(readback)) == 0);
			KASSERT(readback == written);
		}
	}

	{
		uint64_t id_flags = 0;
		void *split;
		void *heap_fixed;
		void *anon;

		KASSERT(is_page_mapped_in_directory(process_pgd(current_process),
						    0x800000UL, &id_flags) == 1);
		KASSERT((id_flags & PAGE_USER) == 0);

		split = ktest_mmap_anon(0x1000, KTEST_PROT_RW,
					KTEST_MAP_PRIVATE | KTEST_MAP_ANONYMOUS |
						KTEST_MAP_FIXED,
					(void *)0x600000UL);
		KASSERT(split == (void *)0x600000UL);
		KASSERT(ktest_pte_present(0x600000UL));
		KASSERT(ktest_pte_user(0x600000UL));
		KASSERT(sys_munmap(split, 0x1000) == 0);

		heap_fixed = ktest_mmap_anon(0x1000, KTEST_PROT_RW,
					     KTEST_MAP_PRIVATE | KTEST_MAP_ANONYMOUS |
						     KTEST_MAP_FIXED,
					     (void *)0x800000UL);
		KASSERT((intptr_t)heap_fixed < 0);

		anon = ktest_mmap_anon(0x4000, KTEST_PROT_RW,
				       KTEST_MAP_PRIVATE | KTEST_MAP_ANONYMOUS,
				       NULL);
		KASSERT(anon != (void *)(intptr_t)-1);
		KASSERT((uintptr_t)anon >= USER_MMAP_START);
		KASSERT((uintptr_t)anon >= IR0_MMAP_NULL_MIN_VA);
		KASSERT(ktest_pte_present((uintptr_t)anon));
		KASSERT(ktest_pte_present((uintptr_t)anon + 0x3000UL));
		KASSERT(sys_munmap(anon, 0x4000) == 0);
	}

	if (fixed)
		KASSERT(sys_munmap(fixed, 0x1000) == 0);
	ktest_unmap_all(slot, 4);

	process_set_heap_start(current_process, saved_heap_start);
	process_set_heap_end(current_process, saved_heap_end);
	process_set_mmap_base(current_process, saved_mmap_base);

	KTEST_END();
}
