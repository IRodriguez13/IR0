/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: paging_fork.c
 * Description: Fork: share the parent's user pages with the child as copy-on-write.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include <config.h>
#include <ir0/logging.h>
#include <ir0/oops.h>
#include <ir0/process.h>
#include <ir0/mm.h>
#include <ir0/debug_runtime.h>
#include <ir0/mm_alloc.h>
#include <ir0/page_table_pool.h>
#include "paging.h"
#include <string.h>
#include <ir0/kmem.h>
#include <mm/pmm.h>
#include <ir0/validation.h>
#include <ir0/arch_port.h>
#include <ir0/tlb_sync.h>
#include <ir0/errno.h>
#include <ir0/swap.h>
#include <ir0/arch_cpu.h>
#include "paging_internal.h"

/*
 * copy_process_memory - Share user pages from parent to child (fork COW)
 * @parent: Parent process
 * @child: Child process
 *
 * Two phases so a failed fork never mutates the parent address space:
 *
 * 1. Map each present PAGE_USER 4KB leaf into the child (share PFN via
 *    pmm_frame_get). Child PTEs for formerly writable pages are RO+PAGE_COW.
 * 2. Only after all child maps succeed, mark the same pages RO+COW in
 *    the parent and flush TLB.
 *
 * Returns: 0 on success, -1 on failure
 */
int copy_process_memory(struct process *parent, struct process *child)
{
    size_t i4;
    size_t i3;
    size_t i2;
    size_t i1;
    uint64_t *parent_root;
    uint64_t *child_root;
    size_t entries;

    if (!parent || !child)
        return -1;

    if (!process_pgd(parent) || !process_pgd(child))
        return -1;

    parent_root = process_pgd(parent);
    child_root = process_pgd(child);
	entries = mm_table_entries();
	if (entries == 0)
		return -1;

    for (i4 = 0; i4 < (size_t)mm_user_root_slots(); i4++)
    {
        uint64_t *level1_table = paging_existing_table(parent_root, i4);

        if (!level1_table)
            continue;

		for (i3 = 0; i3 < entries; i3++)
        {
            uint64_t *level2_table = paging_existing_table(level1_table, i3);

            if (!level2_table)
                continue;

			for (i2 = 0; i2 < entries; i2++)
            {
                uint64_t *level3_table = paging_existing_table(level2_table, i2);

                if (!level3_table)
                    continue;

				for (i1 = 0; i1 < entries; i1++)
                {
                    uint64_t page_entry = level3_table[i1];
                    uint64_t parent_phys;
                    uint64_t flags;
                    uintptr_t virt_addr;
					size_t idx[4];
                    int was_writable;

                    if (mm_pte_is_swap(page_entry))
                    {
                        idx[0] = i4;
                        idx[1] = i3;
                        idx[2] = i2;
                        idx[3] = i1;
                        virt_addr = mm_va_from_indices(idx);
                        if (ir0_swap_dup_pte(page_entry) != 0 ||
                            paging_install_pte(child_root, virt_addr,
                                               page_entry) != 0)
                        {
                            extern void process_unmap_user_pages_all(
                                address_space_root_t root, void *reclaim_stats);

                            (void)ir0_swap_drop_pte(page_entry);
                            process_unmap_user_pages_all(child_root, NULL);
                            return -1;
                        }
                        continue;
                    }
                    if (!mm_pte_present(page_entry) ||
                        !pte_user(page_entry))
                        continue;

                    parent_phys = paging_entry_pfn(page_entry);
					idx[0] = i4;
					idx[1] = i3;
					idx[2] = i2;
					idx[3] = i1;
					virt_addr = mm_va_from_indices(idx);

                    was_writable = pte_write(page_entry) != 0;
					flags = mm_pte_mapping_flags(page_entry);
                    flags &= ~PAGE_GLOBAL;
                    flags |= PAGE_USER;
                    if (was_writable || pte_cow(page_entry))
                    {
                        flags &= ~PAGE_RW;
                        flags |= PAGE_COW;
                    }
                    if (mm_pte_executable(page_entry))
                        flags |= PAGE_EXEC;

                    if (map_page_in_directory(child_root, virt_addr,
                                              parent_phys, flags) != 0)
                    {
                        extern void process_unmap_user_pages_all(
                            address_space_root_t root, void *reclaim_stats);

                        process_unmap_user_pages_all(child_root, NULL);
                        return -1;
                    }

                    pmm_frame_get(parent_phys);
                }
            }
        }
    }

    for (i4 = 0; i4 < (size_t)mm_user_root_slots(); i4++)
    {
        uint64_t *level1_table = paging_existing_table(parent_root, i4);

        if (!level1_table)
            continue;

		for (i3 = 0; i3 < entries; i3++)
        {
            uint64_t *level2_table = paging_existing_table(level1_table, i3);

            if (!level2_table)
                continue;

			for (i2 = 0; i2 < entries; i2++)
            {
                uint64_t *level3_table = paging_existing_table(level2_table, i2);

                if (!level3_table)
                    continue;

				for (i1 = 0; i1 < entries; i1++)
                {
                    uint64_t page_entry = level3_table[i1];

                    if (!mm_pte_present(page_entry) ||
                        !pte_user(page_entry) ||
                        !pte_write(page_entry))
                        continue;

					mm_pte_mark_cow(&level3_table[i1]);
                }
            }
        }
    }

    /*
     * Flush against the parent's address-space root value. Syscalls keep user address-space root, but if
     * we ever run under a different root, reloading only that root would leave
     * stale RW TLB entries and let the parent write shared frames without COW.
     */
    mm_tlb_sync_all(process_pgd(parent));
    return 0;
}

