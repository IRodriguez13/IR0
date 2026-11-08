/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: paging_identity.c
 * Description: Supervisor identity maps (4 KiB, 2 MiB, Device) and user page-table reclaim.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "paging.h"
#include "paging_internal.h"

/*
 * map_supervisor_identity_low - 4 KiB identity map for kernel low memory
 *
 * Boot uses 2 MiB huge pages in address-space root[0]; inheriting that tree blocks user
 * ELF at 0x400000. Each process instead gets fresh 4 KiB supervisor mappings
 * for the low identity range so timer IRQ (TSS RSP0) and syscall entry can
 * run with process address-space root loaded (Linux/BSD: kernel always reachable from user mm).
 */
int map_supervisor_identity_low(address_space_root_t root, uint64_t start,
				uint64_t end)
{
    uint64_t va;

    if (!root || end <= start)
        return -1;

    start &= ~(uint64_t)(PAGE_SIZE_4KB - 1);
    end = (end + PAGE_SIZE_4KB - 1) & ~(uint64_t)(PAGE_SIZE_4KB - 1);

    for (va = start; va < end; va += PAGE_SIZE_4KB)
    {
        /*
         * IRQ/syscall entry runs kernel text under process address-space root; omit NX on
         * the identity map (Linux keeps kernel text executable in every mm).
         */
        if (map_page_in_directory(root, va, va,
                                  PAGE_PRESENT | PAGE_RW | PAGE_EXEC) != 0)
            return -1;
    }

    return 0;
}

/*
 * map_supervisor_identity_2mb - 2 MiB supervisor identity (PD-level PS bit)
 *
 * Cheaper than 4 KiB walks for the kernel heap and PMM frame pool. Callers must
 * not overlap [0x400000, 0x600000): a 2 MiB slot there would block user ELF.
 */
static int map_supervisor_2mb(address_space_root_t root, uint64_t start,
			      uint64_t end, uint64_t flags, int exec)
{
    uint64_t va;

    if (!root || end <= start)
        return -1;

    start = (start + PAGE_SIZE_2MB - 1) & ~((uint64_t)PAGE_SIZE_2MB - 1);
    end &= ~((uint64_t)PAGE_SIZE_2MB - 1);
    if (end <= start)
        return 0;

    for (va = start; va < end; va += PAGE_SIZE_2MB)
    {
        size_t idx[4];
        uint64_t *level1_table;
        uint64_t *level2_table;
        uint64_t entry;

        mm_va_indices((uintptr_t)va, idx);

        level1_table = paging_get_or_create_table(root, root, idx[0], 1, 0, 1);
        if (!level1_table)
            return -1;

        level2_table = paging_get_or_create_table(root, level1_table, idx[1], 1, 0, 2);
        if (!level2_table)
            return -1;

        if (mm_pte_present(level2_table[idx[2]]))
        {
            if (paging_entry_large(level2_table[idx[2]]))
                continue;
            return -1;
        }

        entry = mm_make_leaf_pte((uintptr_t)va,
                                 flags | PAGE_SIZE_2MB_FLAG, exec);
        level2_table[idx[2]] = entry;
        ir0_mm_leaf_created++;
    }

    return 0;
}

int map_supervisor_identity_2mb(address_space_root_t root, uint64_t start,
				uint64_t end)
{
    if (!mm_large_identity_supported())
        return map_supervisor_identity_low(root, start, end);
    return map_supervisor_2mb(root, start, end, PAGE_RW, 1);
}

/*
 * map_supervisor_device_2mb - 2 MiB uncached, non-executable supervisor
 * identity for MMIO windows (Device memory on ARM64).
 */
int map_supervisor_device_2mb(address_space_root_t root, uint64_t start,
			      uint64_t end)
{
    return map_supervisor_2mb(root, start, end,
                              PAGE_RW | IR0_MM_MAP_NOCACHE, 0);
}

void paging_reclaim_user_tables(address_space_root_t root)
{
    size_t i4;
    size_t entries;

    if (!root)
        return;
    entries = mm_table_entries();
    if (entries == 0)
        return;

    for (i4 = 0; i4 < (size_t)mm_user_root_slots(); i4++)
    {
        uint64_t root_entry = root[i4];
        uint64_t *level1_table;
        size_t i3;

        if (!mm_pte_present(root_entry) || paging_entry_large(root_entry))
            continue;

        level1_table = paging_entry_table(root_entry);
        if (!level1_table)
            continue;

		for (i3 = 0; i3 < entries; i3++)
        {
            uint64_t level1_entry = level1_table[i3];
            uint64_t *level2_table;
            size_t i2;

            if (!mm_pte_present(level1_entry) || paging_entry_large(level1_entry))
                continue;

            level2_table = paging_entry_table(level1_entry);
            if (!level2_table)
                continue;

			for (i2 = 0; i2 < entries; i2++)
            {
                uint64_t level2_entry = level2_table[i2];
                uint64_t *level3_table;
                size_t i1;

                if (!mm_pte_present(level2_entry))
                    continue;

                /* Supervisor 2 MiB identity: drop PDE only (PFN is not a PT). */
                if (paging_entry_large(level2_entry))
                {
                    level2_table[i2] = 0;
                    continue;
                }

                level3_table = paging_entry_table(level2_entry);
                if (!level3_table)
                    continue;

				for (i1 = 0; i1 < entries; i1++)
                    level3_table[i1] = 0;

                level2_table[i2] = 0;
                ir0_mm_free_table_frame(paging_entry_pfn(level2_entry), 3);
            }

            level1_table[i3] = 0;
            ir0_mm_free_table_frame(paging_entry_pfn(level1_entry), 2);
        }

        root[i4] = 0;
        ir0_mm_free_table_frame(paging_entry_pfn(root_entry), 1);
    }
}

