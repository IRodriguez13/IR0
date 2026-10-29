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
#include "process.h"
#include "process/process_internal.h"
#include "syscalls.h"
#include <ir0/abi/mmap_contract.h>
#include <ir0/kmem.h>
#include <ir0/paging.h>
#include <ir0/pmm.h>
#include <ir0/process.h>
#include <ir0/sched.h>
#include <stdint.h>
#include <string.h>

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

static void ktest_reclaim_child_entry(void)
{
	process_exit(0);
}

void ktest_reclaim_freed_page_tables_safely(void)
{
	pid_t pid;
	process_t *child;
	address_space_root_t root;
	uintptr_t va = USER_MMAP_END - PAGE_SIZE_4KB;
	uint64_t phys = 0;
	process_reclaim_stats_t stats;

	KTEST_BEGIN("reclaim_freed_page_tables_safely");
	pid = spawn_kernel(ktest_reclaim_child_entry, "reclaim_child");
	KASSERT_GT(pid, 0);
	child = pid > 0 ? process_find_by_pid(pid) : NULL;
	KASSERT(child != NULL);
	if (!child)
	{
		KTEST_END();
		return;
	}

	root = process_pgd(child);
	KASSERT(root != NULL);
	if (root)
	{
		phys = pmm_alloc_frame();
		KASSERT(phys != 0);
		if (phys != 0)
		{
			int mapped = map_page_in_directory(root, va, phys,
						 PAGE_USER | PAGE_RW);

			KASSERT_EQ(mapped, 0);
			if (mapped == 0)
			{
				KASSERT(is_page_mapped_in_directory(root, va,
								    NULL) == 1);

				memset(&stats, 0, sizeof(stats));
				/*
				 * Make the UAF visible: a table freed mid-walk
				 * is overwritten with 0xA5 instead of keeping
				 * stale zeros.
				 */
				paging_test_poison_freed_tables(1);
				process_unmap_user_pages_all(root, &stats);
				paging_test_poison_freed_tables(0);

				/*
				 * Exactly one user PTE existed. The old walk
				 * freed its page table and then kept reading
				 * the freed entries, counting every poisoned
				 * slot as a mapping.
				 */
				KASSERT_EQ(stats.mapped_pages, 1ULL);
				KASSERT_EQ(stats.freed_pages, 1ULL);
				KASSERT_EQ(stats.missing_pages, 0ULL);
				KASSERT(is_page_mapped_in_directory(root, va,
								    NULL) == 0);
			}
			else
			{
				pmm_free_frame(phys);
			}
		}
	}

	process_mark_zombie(child);
	child->exit_code = 0;
	sched_remove_process(child);
	KASSERT_EQ(process_wait(pid, NULL, 0), pid);
	KTEST_END();
}

static int ktest_maps_parse_hex(const char **pp, uint64_t *out)
{
	const char *p = *pp;
	uint64_t value = 0;
	int digits = 0;

	for (;;)
	{
		char c = *p;
		int d;

		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (c >= 'a' && c <= 'f')
			d = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			d = c - 'A' + 10;
		else
			break;
		if (value > (UINT64_MAX >> 4))
			return -1;
		value = (value << 4) | (uint64_t)d;
		p++;
		digits++;
	}
	if (digits == 0)
		return -1;
	*pp = p;
	*out = value;
	return 0;
}

/*
 * Read /proc/self/maps and return the four permission characters for the
 * exact [start, end) range.  A missing range or a stale VMA prot fails.
 */
static int ktest_maps_perms(uintptr_t start, uintptr_t end, char perms[5])
{
	char buf[4096];
	const char *line;
	int64_t fd;
	int64_t n;

	fd = sys_open("/proc/self/maps", 0, 0);
	if (fd <= 0)
		return -1;
	memset(buf, 0, sizeof(buf));
	n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	if (n <= 0)
		return -1;

	line = buf;
	while (*line)
	{
		const char *p = line;
		uint64_t line_start;
		uint64_t line_end;

		if (ktest_maps_parse_hex(&p, &line_start) == 0 && *p == '-')
		{
			p++;
			if (ktest_maps_parse_hex(&p, &line_end) == 0 &&
			    line_start == (uint64_t)start &&
			    line_end == (uint64_t)end && *p == ' ')
			{
				memcpy(perms, p + 1, 4);
				perms[4] = '\0';
				return 0;
			}
		}
		line = strchr(line, '\n');
		if (!line)
			break;
		line++;
	}
	return -1;
}

void ktest_mprotect_updates_maps(void)
{
	void *region;
	uintptr_t base;
	uint64_t saved_heap_start;
	uint64_t saved_heap_end;
	uint64_t saved_mmap_base;
	char perms[5];
	int rc;

	KTEST_BEGIN("mprotect_updates_maps");
	KASSERT(current_process != NULL);

	saved_heap_start = process_heap_start(current_process);
	saved_heap_end = process_heap_end(current_process);
	saved_mmap_base = process_mmap_base(current_process);
	process_set_heap_start(current_process, KTEST_BUSYBOX_INITIAL_BRK);
	process_set_heap_end(current_process, KTEST_BUSYBOX_INITIAL_BRK);
	process_set_mmap_base(current_process, 0);

	region = ktest_mm_mmap_anon(3 * PAGE_SIZE_4KB, KTEST_PROT_RW,
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
	 * Protect only the middle page.  Linux splits the VMA into three, so
	 * maps must report rw-p, r--p, rw-p.  A single line covering all
	 * three pages proves the VMA metadata was not split with the PTEs.
	 */
	rc = sys_mprotect((void *)(base + PAGE_SIZE_4KB), PAGE_SIZE_4KB,
			  KTEST_PROT_READ);
	KASSERT_EQ(rc, 0);
	KASSERT_EQ(ktest_maps_perms(base, base + PAGE_SIZE_4KB, perms), 0);
	KASSERT(strcmp(perms, "rw-p") == 0);
	KASSERT_EQ(ktest_maps_perms(base + PAGE_SIZE_4KB,
				    base + 2 * PAGE_SIZE_4KB, perms), 0);
	KASSERT(strcmp(perms, "r--p") == 0);
	KASSERT_EQ(ktest_maps_perms(base + 2 * PAGE_SIZE_4KB,
				    base + 3 * PAGE_SIZE_4KB, perms), 0);
	KASSERT(strcmp(perms, "rw-p") == 0);

	/*
	 * Restoring the whole range must update every split VMA and coalesce
	 * them back into one, as Linux does; otherwise repeated split/restore
	 * cycles fragment the VMA list.
	 */
	rc = sys_mprotect(region, 3 * PAGE_SIZE_4KB, KTEST_PROT_RW);
	KASSERT_EQ(rc, 0);
	KASSERT_EQ(ktest_maps_perms(base, base + 3 * PAGE_SIZE_4KB, perms),
		  0);
	KASSERT(strcmp(perms, "rw-p") == 0);

	/* And a left-edge split on the first of the three VMAs. */
	rc = sys_mprotect(region, PAGE_SIZE_4KB, KTEST_PROT_READ);
	KASSERT_EQ(rc, 0);
	KASSERT_EQ(ktest_maps_perms(base, base + PAGE_SIZE_4KB, perms), 0);
	KASSERT(strcmp(perms, "r--p") == 0);
	KASSERT_EQ(ktest_maps_perms(base + PAGE_SIZE_4KB,
				    base + 3 * PAGE_SIZE_4KB, perms), 0);
	KASSERT(strcmp(perms, "rw-p") == 0);

	KASSERT_EQ(sys_munmap(region, 3 * PAGE_SIZE_4KB), 0);
	ktest_mm_cleanup(base, 3 * PAGE_SIZE_4KB);

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
