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
#include <stdint.h>

#define KTEST_BUSYBOX_INITIAL_BRK 0x453000UL
#define KTEST_BRK_GROW 0x2000UL

static int ktest_pte_present(uintptr_t va)
{
	uint64_t flags = 0;

	if (!current_process || !current_process->page_directory)
		return 0;
	return is_page_mapped_in_directory(current_process->page_directory, va,
					   &flags) == 1;
}

void ktest_brk_post_exec(void)
{
	uint64_t saved_start;
	uint64_t saved_end;
	int64_t cur;
	int64_t grown;

	KTEST_BEGIN("brk_post_exec");

	saved_start = current_process->heap_start;
	saved_end = current_process->heap_end;

	current_process->heap_start = KTEST_BUSYBOX_INITIAL_BRK;
	current_process->heap_end = KTEST_BUSYBOX_INITIAL_BRK;

	cur = sys_brk(NULL);
	KASSERT_EQ((uint64_t)cur, KTEST_BUSYBOX_INITIAL_BRK);

	grown = sys_brk((void *)(KTEST_BUSYBOX_INITIAL_BRK + KTEST_BRK_GROW));
	KASSERT_EQ((uint64_t)grown, KTEST_BUSYBOX_INITIAL_BRK + KTEST_BRK_GROW);
	KASSERT(ktest_pte_present(KTEST_BUSYBOX_INITIAL_BRK));
	KASSERT(ktest_pte_present(KTEST_BUSYBOX_INITIAL_BRK + 0x1000UL));

	current_process->heap_start = saved_start;
	current_process->heap_end = saved_end;

	KTEST_END();
}
