/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_brk_post_exec.c
 * Description: ktest — Linux brk ABI after ELF-style initial break
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test/ktest_harness.h"
#include "syscalls.h"
#include <ir0/abi/brk_contract.h>
#include <ir0/paging.h>
#include <ir0/process.h>
#include <config.h>
#include <stdint.h>

#define KTEST_BUSYBOX_INITIAL_BRK 0x453000UL
#define KTEST_BRK_GROW 0x2000UL
#define KTEST_BRK_ACROSS_6M 0x601000UL

#define KTEST_BRK_PAGE_COUNT \
	((KTEST_BRK_ACROSS_6M - KTEST_BUSYBOX_INITIAL_BRK) / PAGE_SIZE_4KB)

struct ktest_brk_page {
	uint8_t present;
	uint8_t user;
};

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

static void ktest_brk_snapshot(struct ktest_brk_page *state)
{
	size_t i;

	for (i = 0; i < KTEST_BRK_PAGE_COUNT; i++)
	{
		uintptr_t va = KTEST_BUSYBOX_INITIAL_BRK + (i * PAGE_SIZE_4KB);
		uint64_t flags = 0;

		state[i].present = (uint8_t)ktest_pte_state(va, &flags);
		state[i].user = (uint8_t)(state[i].present &&
					  (flags & PAGE_USER) != 0);
	}
}

/*
 * brk growth installs USER leaves over the supervisor identity window. Undo
 * only the pages this test changed: pages that became USER (absent or
 * supervisor before) are unmapped, and supervisor pages get their identity
 * mapping back. Pre-existing heap pages and untouched identity leaves stay.
 */
static void ktest_brk_restore(const struct ktest_brk_page *state)
{
	address_space_root_t root;
	size_t i;

	if (!current_process || !process_pgd(current_process))
		return;
	root = process_pgd(current_process);

	for (i = 0; i < KTEST_BRK_PAGE_COUNT; i++)
	{
		uintptr_t va = KTEST_BUSYBOX_INITIAL_BRK + (i * PAGE_SIZE_4KB);
		uint64_t flags = 0;

		if (!ktest_pte_state(va, &flags) || !(flags & PAGE_USER) ||
		    state[i].user)
			continue;
		(void)unmap_page_in_directory(root, va);
		if (state[i].present)
			(void)map_page_in_directory(root, va, va, PAGE_RW);
	}
}

void ktest_brk_post_exec(void)
{
	struct ktest_brk_page state[KTEST_BRK_PAGE_COUNT];
	uint64_t saved_start;
	uint64_t saved_end;
	int64_t cur;
	int64_t grown;

	KTEST_BEGIN("brk_post_exec");

	saved_start = process_heap_start(current_process);
	saved_end = process_heap_end(current_process);
	ktest_brk_snapshot(state);

	process_set_heap_start(current_process, KTEST_BUSYBOX_INITIAL_BRK);
	process_set_heap_end(current_process, KTEST_BUSYBOX_INITIAL_BRK);

	cur = sys_brk(NULL);
	KASSERT_EQ((uint64_t)cur, KTEST_BUSYBOX_INITIAL_BRK);

	grown = sys_brk((void *)(KTEST_BUSYBOX_INITIAL_BRK + KTEST_BRK_GROW));
	KASSERT_EQ((uint64_t)grown, KTEST_BUSYBOX_INITIAL_BRK + KTEST_BRK_GROW);
	KASSERT(ktest_pte_present(KTEST_BUSYBOX_INITIAL_BRK));
	KASSERT(ktest_pte_user(KTEST_BUSYBOX_INITIAL_BRK));
	KASSERT(ktest_pte_present(KTEST_BUSYBOX_INITIAL_BRK + 0x1000UL));
	KASSERT(ktest_pte_user(KTEST_BUSYBOX_INITIAL_BRK + 0x1000UL));

	/* Cross the 6 MiB supervisor-2MB identity seam (Linux split_huge_pmd). */
	grown = sys_brk((void *)KTEST_BRK_ACROSS_6M);
	KASSERT_EQ((uint64_t)grown, KTEST_BRK_ACROSS_6M);
	KASSERT(ktest_pte_present(0x600000UL));
	KASSERT(ktest_pte_user(0x600000UL));

	process_set_heap_start(current_process, saved_start);
	process_set_heap_end(current_process, saved_end);
	ktest_brk_restore(state);
	KASSERT_EQ(process_heap_start(current_process), saved_start);
	KASSERT_EQ(process_heap_end(current_process), saved_end);

	KTEST_END();
}

void ktest_brk_linux_fail_returns_current(void)
{
	uint64_t saved_start;
	uint64_t saved_end;
	int64_t cur;
	int64_t ret;

	KTEST_BEGIN("brk_linux_fail_returns_current");

	saved_start = process_heap_start(current_process);
	saved_end = process_heap_end(current_process);

	process_set_heap_start(current_process, KTEST_BUSYBOX_INITIAL_BRK);
	process_set_heap_end(current_process, KTEST_BUSYBOX_INITIAL_BRK);

	cur = sys_brk(NULL);
	KASSERT_EQ((uint64_t)cur, KTEST_BUSYBOX_INITIAL_BRK);

	/* Below heap_lo: Linux keeps the old break (not -EFAULT). */
	ret = sys_brk((void *)(KTEST_BUSYBOX_INITIAL_BRK - 1UL));
	KASSERT_EQ((uint64_t)ret, (uint64_t)cur);

	/* Past USER_HEAP_MAX_SIZE: same contract. */
	ret = sys_brk((void *)(KTEST_BUSYBOX_INITIAL_BRK + USER_HEAP_MAX_SIZE +
			       PAGE_SIZE_4KB));
	KASSERT_EQ((uint64_t)ret, (uint64_t)cur);
	KASSERT_EQ((uint64_t)sys_brk(NULL), (uint64_t)cur);

	process_set_heap_start(current_process, saved_start);
	process_set_heap_end(current_process, saved_end);

	KTEST_END();
}
