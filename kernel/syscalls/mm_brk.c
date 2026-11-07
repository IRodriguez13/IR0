/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mm_brk.c
 * Description: brk(2): grow or shrink the program break without overlapping mmap regions.
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
 * Does [lo, hi) overlap any registered mmap region of @proc?
 * Linux find_vma_intersection equivalent for the brk growth check.
 */
static int brk_range_hits_mmap(process_t *proc, uintptr_t lo, uintptr_t hi)
{
	struct mmap_region *r;

	for (r = process_mmap_list(proc); r != NULL; r = r->next)
	{
		uintptr_t r_lo = (uintptr_t)r->addr;
		uintptr_t r_hi = r_lo + (r->length ? r->length : 1);

		if (lo < r_hi && r_lo < hi)
			return 1;
	}
	return 0;
}

int64_t sys_brk(void *addr)
{
	uintptr_t heap_lo;
	uintptr_t new_brk;
	uintptr_t current_brk;

	if (!current_process)
		return -ESRCH;

	klog_debug_fmt("BRK", "req pid=%x addr=%llx hs=%llx he=%llx\n",
			(unsigned)((uint32_t)current_process->task.pid),
			(unsigned long long)(uintptr_t)addr,
			(unsigned long long)(uint64_t)process_heap_start(current_process),
			(unsigned long long)(uint64_t)process_heap_end(current_process));

	/* brk(NULL) / brk(0): return current program break (Linux ABI). */
	if (!addr)
	{
		if (process_heap_start(current_process) == 0 &&
		    process_heap_end(current_process) == 0)
		{
			process_set_heap_start(current_process, USER_HEAP_BASE);
			process_set_heap_end(current_process, USER_HEAP_BASE);
		}
		return (int64_t)process_heap_end(current_process);
	}

	new_brk = (uintptr_t)addr;
	current_brk = process_heap_end(current_process);
	heap_lo = process_heap_start(current_process);

	/*
	 * Processes without ELF exec init (legacy smokes): fall back to
	 * USER_HEAP_BASE. Post-exec images set heap_start/end from PT_LOAD
	 * (already snapped to USER_HEAP_BASE by elf_compute_initial_brk).
	 */
	if (heap_lo == 0 && current_brk == 0)
	{
		heap_lo = USER_HEAP_BASE;
		process_set_heap_start(current_process, heap_lo);
		process_set_heap_end(current_process, heap_lo);
		current_brk = heap_lo;
	}

	/*
	 * Linux brk(2) never returns -errno. A rejected request keeps the
	 * current program break so musl's `__syscall(SYS_brk) < end` probe
	 * sees a real VA instead of -EFAULT.
	 */
	if (!is_user_address(addr, 0) || new_brk < heap_lo ||
	    new_brk > heap_lo + USER_HEAP_MAX_SIZE)
		return (int64_t)current_brk;

	/*
	 * Linux do_brk_flags: the break never grows over an existing mapping.
	 * musl probes the page above the break with a PROT_NONE MAP_FIXED
	 * anonymous mmap; extending across it left the kernel treating the page
	 * as heap while the VMA still said PROT_NONE, so the first heap access
	 * took a SIGSEGV. Returning the unchanged break is how brk(2) reports
	 * failure, and musl then falls back to mmap.
	 */
	/*
	 * Linux SYSCALL_DEFINE1(brk) also reserves a guard page past the new
	 * break (find_vma_intersection(mm, oldbrk, newbrk + PAGE_SIZE)). IR0
	 * cannot: mallocng parks its PROT_NONE guard exactly one page above
	 * the break it just set, so the extra page rejects musl's own growth.
	 */
	if (new_brk > current_brk &&
	    brk_range_hits_mmap(current_process, current_brk, new_brk))
		return (int64_t)current_brk;

	/* If expanding heap, map only pages past the current break. */
	if (new_brk > current_brk)
	{
		uintptr_t start_page =
			(current_brk + PAGE_SIZE_4KB - 1) &
			(uintptr_t)PAGE_FRAME_MASK;
		uintptr_t end_page = (new_brk + PAGE_SIZE_4KB - 1) &
				     (uintptr_t)PAGE_FRAME_MASK;
		size_t size_to_map;

		if (end_page > start_page)
		{
			size_to_map = end_page - start_page;
			if (map_user_region_in_directory(process_pgd(current_process),
							 start_page, size_to_map,
							 PAGE_RW) != 0)
				return (int64_t)process_heap_end(current_process);
		}
	}
	else if (new_brk < current_brk)
	{
		uintptr_t old_end = current_brk;

		for (uintptr_t page = (new_brk + (PAGE_SIZE_4KB - 1)) &
				      (uintptr_t)PAGE_FRAME_MASK;
		     page < old_end;
		     page += PAGE_SIZE_4KB)
			unmap_page_in_directory(process_pgd(current_process), page);
	}

	process_set_heap_end(current_process, new_brk);
	return (int64_t)new_brk;
}

/* sbrk is typically implemented as a userspace library function using brk */
/* POSIX does not require sbrk as a syscall */

