/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: paging_uaccess.c
 * Description: Kernel copies to and from user memory through the page tables, breaking COW like a write fault.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/copy_user.h>
#include <ir0/cpu.h>
#include <ir0/mm_alloc.h>
#include <ir0/tlb_sync.h>
#include <mm/pmm.h>
#include <string.h>
#include "paging.h"
#include "paging_internal.h"

/*
 * Copy or zero user memory via mapped physical frames. Process address-space root may omit
 * PMM identity (Linux-like holes); switch to the pinned boot address-space root for PA
 * access (same as paging_copy_phys_page / paging_zero_phys_page).
 */

/*
 * Give the COW page at @vaddr a private copy of its shared frame, mapped
 * writable or read-only. Kernel writes via PA identity must not touch a
 * RO+COW shared frame (that bypasses hardware WP and corrupts the sibling
 * mm), and mprotect without PROT_WRITE must stop a later write from copying.
 */
int paging_cow_unshare(address_space_root_t root, uintptr_t vaddr, int writable)
{
	uintptr_t page = vaddr & (uintptr_t)PAGE_FRAME_MASK;
	uint64_t *pte;
	uint64_t entry;
	uintptr_t old_phys;
	uintptr_t new_phys;
	uint64_t map_flags;
	unsigned long irq_flags;

	if (!root)
		return -1;
	irq_flags = irq_save();
	pte = paging_get_pte(root, page);
	if (!pte || !pte_present(*pte) || !pte_cow(*pte) || !pte_user(*pte))
	{
		irq_restore(irq_flags);
		return -1;
	}
	entry = *pte;
	old_phys = mm_pte_phys(entry);
	new_phys = mm_alloc_frame(MM_ALLOC_ATOMIC);
	if (!new_phys)
	{
		irq_restore(irq_flags);
		return -1;
	}
	paging_copy_phys_page(new_phys, old_phys);
	map_flags = mm_pte_mapping_flags(entry) | PAGE_USER;
	map_flags &= ~(PAGE_COW | PAGE_GLOBAL | PAGE_RW);
	if (writable)
		map_flags |= PAGE_RW;
	if (mm_pte_executable(entry))
		map_flags |= PAGE_EXEC;
	if (map_page_in_directory(root, page, new_phys, map_flags) != 0)
	{
		pmm_free_frame(new_phys);
		irq_restore(irq_flags);
		return -1;
	}
	mm_tlb_sync_page(root, page);
	if (map_flags & PAGE_EXEC)
		paging_sync_icache_user(root, page, PAGE_SIZE_4KB);
	irq_restore(irq_flags);
	return 0;
}

static int paging_resolve_user_page(address_space_root_t root, uintptr_t page,
				     int write, uintptr_t *phys_out)
{
	uint64_t *pte;

	if (!root || !phys_out)
		return -1;
	pte = paging_get_pte(root, page);
	/*
	 * Only user-accessible leaves count: the low user VA window also holds
	 * supervisor identity pages (x86_64 keyboard ring), and a syscall must
	 * not read or write them on behalf of userspace.
	 */
	if (!pte || !pte_user(*pte) || pte_protnone(*pte))
		return -1;
	/*
	 * A write needs a page the process could write itself: writable, or
	 * COW (broken here like a write fault). Read-only pages, including
	 * image pages a fork shares without COW, fail like Linux uaccess.
	 */
	if (write && !pte_write(*pte))
	{
		if (paging_cow_unshare(root, page, 1) != 0)
			return -1;
		pte = paging_get_pte(root, page);
		if (!pte || !mm_pte_present(*pte) || !pte_write(*pte))
			return -1;
	}
	*phys_out = paging_entry_pfn(*pte);
	return 0;
}

int copy_to_user_mm(address_space_root_t root, uintptr_t dst, const void *src,
		    size_t n)
{
    const uint8_t *s = src;

    if (n == 0)
        return 0;
    if (!root || !src || !mm_user_va_ok(dst, n))
        return -EFAULT;

    while (n > 0)
    {
        uintptr_t page = dst & (uintptr_t)PAGE_FRAME_MASK;
        uintptr_t phys;
        size_t off;
        size_t chunk;

        if (paging_resolve_user_page(root, page, 1, &phys) != 0)
            return -EFAULT;
        off = dst & (uintptr_t)PAGE_OFFSET_MASK;
        chunk = (size_t)(PAGE_SIZE_4KB - off);
        if (chunk > n)
            chunk = n;

	{
		uintptr_t saved;

		paging_phys_map_begin(&saved);
		memcpy((void *)(phys + off), s, chunk);
		paging_phys_map_end(saved);
	}
        dst += chunk;
        s += chunk;
        n -= chunk;
    }

    return 0;
}

int copy_from_user_mm(address_space_root_t root, uintptr_t src, void *dst,
		      size_t n)
{
    uint8_t *d = dst;

    if (n == 0)
        return 0;
    if (!root || !dst || !mm_user_va_ok(src, n))
        return -EFAULT;

    while (n > 0)
    {
        uintptr_t page = src & (uintptr_t)PAGE_FRAME_MASK;
        uintptr_t phys;
        size_t off;
        size_t chunk;

        if (paging_resolve_user_page(root, page, 0, &phys) != 0)
            return -EFAULT;
        off = src & (uintptr_t)PAGE_OFFSET_MASK;
        chunk = (size_t)(PAGE_SIZE_4KB - off);
        if (chunk > n)
            chunk = n;

	{
		uintptr_t saved;

		paging_phys_map_begin(&saved);
		memcpy(d, (const void *)(phys + off), chunk);
		paging_phys_map_end(saved);
	}
        src += chunk;
        d += chunk;
        n -= chunk;
    }

    return 0;
}

int clear_user_mm(address_space_root_t root, uintptr_t dst, size_t n)
{
    if (n == 0)
        return 0;
    if (!root || !mm_user_va_ok(dst, n))
        return -EFAULT;

    while (n > 0)
    {
        uintptr_t page = dst & (uintptr_t)PAGE_FRAME_MASK;
        uintptr_t phys;
        size_t off;
        size_t chunk;

        if (paging_resolve_user_page(root, page, 1, &phys) != 0)
            return -EFAULT;
        off = dst & (uintptr_t)PAGE_OFFSET_MASK;
        chunk = (size_t)(PAGE_SIZE_4KB - off);
        if (chunk > n)
            chunk = n;

	{
		uintptr_t saved;

		paging_phys_map_begin(&saved);
		memset((void *)(phys + off), 0, chunk);
		paging_phys_map_end(saved);
	}
        dst += chunk;
        n -= chunk;
    }

    return 0;
}

