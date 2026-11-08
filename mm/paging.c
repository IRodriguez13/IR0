/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: paging.c
 * Description: Page tables, 2 MiB identity split, and leaf install.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/page_table_pool.h>
#include "paging.h"
#include "paging_internal.h"
#include <string.h>
#include <mm/pmm.h>
#include <ir0/tlb_sync.h>
#include <ir0/swap.h>
/*
 * Boot/kernel address-space root with full PMM identity. Pinned once before
 * any process mm. Used like Linux kmap for physical page copy/zero
 * after process tables punch identity holes to pte_none.
 */
static uintptr_t paging_kernel_root;
void paging_pin_kernel_address_space(uintptr_t root)
{
	if (paging_kernel_root || !root)
		return;
	paging_kernel_root = root;
}
#include <ir0/arch_cpu.h>

static int page_table_is_empty(const uint64_t *table)
{
    size_t entries = mm_table_entries();

    if (!table || entries == 0)
        return 1;
    for (size_t i = 0; i < entries; i++)
    {
        if (mm_pte_present(table[i]))
            return 0;
    }
    return 1;
}

/*
 * ktest hook: overwrite a table before freeing it so a walk that keeps using
 * the old pointer sees garbage instead of silently reading zeros. Disabled
 * outside the test that requests it.
 */
static int paging_test_poison_freed;

/* Fill byte used to make a stale page-table walk visible to a test. */
#define PAGING_TEST_POISON_BYTE 0xA5

void paging_test_poison_freed_tables(int enable)
{
    paging_test_poison_freed = !!enable;
}

void ir0_mm_free_table_frame(uint64_t frame, int level)
{
    if (!frame)
        return;

    if (level == 1)
        ir0_mm_level1_freed++;
    else if (level == 2)
        ir0_mm_level2_freed++;
    else if (level == 3)
        ir0_mm_level3_freed++;

    if (paging_test_poison_freed)
        memset((void *)(uintptr_t)frame, PAGING_TEST_POISON_BYTE, PAGE_SIZE_4KB);

    ir0_mm_set_frame_type(frame, IR0_MM_FRAME_UNKNOWN);
    ir0_mm_frame_pt_free++;
    ir0_mm_log_frame_type("FREE", frame, IR0_MM_FRAME_PT);
    page_table_frame_free((uintptr_t)frame);
}


void enable_paging(void)
{
	mm_enable_translation();
	paging_pin_kernel_address_space(paging_current_address_space());
}

void setup_and_enable_paging(void)
{
	/* SILENT safety checks */
	/* DO NOT use print/log during critical setup */

	if (!mm_translation_ready())
		return;

	/* Verify address translation is enabled. */
	if (!is_paging_enabled())
	{
		/* Paging not enabled - enable it */
		enable_paging();
	}
	else
		paging_pin_kernel_address_space(paging_current_address_space());
}

void paging_sync_kernel_mappings(address_space_root_t root)
{
	uintptr_t kernel_root = paging_kernel_root;

	if (!root || !kernel_root)
		return;
	if ((uintptr_t)root == kernel_root)
		return;
	mm_copy_kernel_half(root, (address_space_root_t)kernel_root);
}

void paging_activate_address_space(uintptr_t root)
{
	/*
	 * Switch while RSP may sit on a per-task kstack: ensure this mm still
	 * links the ISA-defined shared translation subtree (new kstack slots).
	 */
	if (root)
		mm_prepare_root_activation((address_space_root_t)root);
	mm_activate(root);
}

uintptr_t paging_current_address_space(void)
{
	return mm_current_root();
}

uintptr_t paging_kernel_address_space(void)
{
	/* Never lazy-capture from a process root: it may lack kernel mappings. */
	return paging_kernel_root;
}

void paging_phys_map_begin(uintptr_t *saved_root)
{
	*saved_root = paging_current_address_space();
	if (paging_kernel_root && paging_kernel_root != *saved_root)
		paging_activate_address_space(paging_kernel_root);
}

void paging_phys_map_end(uintptr_t saved_root)
{
	if (paging_kernel_root && paging_kernel_root != saved_root)
	{
		paging_activate_address_space(saved_root);
	}
}

/*
 * Phys↔phys page copy (Linux copy_user_highpage / kmap spirit): run under the
 * pinned boot address-space root so process tables may leave heap holes as pte_none.
 */
void paging_copy_phys_page(uintptr_t dst_phys, uintptr_t src_phys)
{
	uintptr_t saved;

	paging_phys_map_begin(&saved);
	memcpy((void *)dst_phys, (void *)src_phys, PAGE_SIZE_4KB);
	paging_phys_map_end(saved);
}

void paging_zero_phys_page(uintptr_t phys)
{
	uintptr_t saved;

	paging_phys_map_begin(&saved);
	memset((void *)phys, 0, PAGE_SIZE_4KB);
	paging_phys_map_end(saved);
}

/*
 * Poison a page so unused kernel stack stays recognisable.
 *
 * Sampling headroom from the interrupt path only sees the depths an
 * interrupt happens to land on, and it missed a getdents chain that spent
 * 20 KiB. Filling the stack at allocation lets the untouched remainder be
 * measured directly, the way Linux DEBUG_STACK_USAGE does.
 */
void paging_poison_phys_page(uintptr_t phys, uint8_t pattern)
{
	uintptr_t saved;

	paging_phys_map_begin(&saved);
	memset((void *)phys, (int)pattern, PAGE_SIZE_4KB);
	paging_phys_map_end(saved);
}

int is_paging_enabled(void)
{
	return mm_translation_enabled();
}

/**
 * Check if a virtual address is mapped in a page directory
 * @root: address-space root table address (page directory)
 * @virt_addr: Virtual address to check
 * @flags_out: Optional output for page flags (can be NULL)
 * Returns: 1 if mapped, 0 if not mapped, -1 on error
 */
int is_page_mapped_in_directory(address_space_root_t root, uint64_t virt_addr,
				uint64_t *flags_out)
{
	size_t idx[4];
	uint64_t *level1_table;
	uint64_t *level2_table;
	uint64_t *level3_table;

	if (!root)
		return -1;

	mm_va_indices((uintptr_t)virt_addr, idx);

	if (!mm_pte_present(root[idx[0]]))
		return 0;
	if (paging_entry_large(root[idx[0]]))
	{
		if (flags_out)
			*flags_out = mm_pte_mapping_flags(root[idx[0]]);
		return 1;
	}

	level1_table = paging_entry_table(root[idx[0]]);
	if (!level1_table || !mm_pte_present(level1_table[idx[1]]))
		return 0;
	if (paging_entry_large(level1_table[idx[1]]))
	{
		if (flags_out)
			*flags_out = mm_pte_mapping_flags(level1_table[idx[1]]);
		return 1;
	}

	level2_table = paging_entry_table(level1_table[idx[1]]);
	if (!level2_table || !mm_pte_present(level2_table[idx[2]]))
		return 0;
	if (paging_entry_large(level2_table[idx[2]]))
	{
		/* Linux pmd_present && pmd_large: a 2 MiB leaf is mapped. */
		if (flags_out)
			*flags_out = mm_pte_mapping_flags(level2_table[idx[2]]);
		return 1;
	}

	level3_table = paging_entry_table(level2_table[idx[2]]);
	if (!level3_table || !mm_pte_present(level3_table[idx[3]]))
		return 0;

	if (flags_out)
		*flags_out = mm_pte_mapping_flags(level3_table[idx[3]]);

	return 1;
}

uint64_t *paging_get_pte(address_space_root_t root, uintptr_t vaddr)
{
	size_t idx[4];
	uint64_t *level1_table;
	uint64_t *level2_table;
	uint64_t *level3_table;

	if (!root)
		return NULL;

	mm_va_indices(vaddr, idx);

	if (!mm_pte_present(root[idx[0]]))
		return NULL;
	if (paging_entry_large(root[idx[0]]))
		return NULL;

	level1_table = paging_entry_table(root[idx[0]]);
	if (!level1_table || !mm_pte_present(level1_table[idx[1]]))
		return NULL;
	if (paging_entry_large(level1_table[idx[1]]))
		return NULL;

	level2_table = paging_entry_table(level1_table[idx[1]]);
	if (!level2_table || !mm_pte_present(level2_table[idx[2]]))
		return NULL;
	if (paging_entry_large(level2_table[idx[2]]))
		return NULL;

	level3_table = paging_entry_table(level2_table[idx[2]]);
	if (!level3_table)
		return NULL;
	return &level3_table[idx[3]];
}

/**
 * Simple page table getter - only returns existing tables
 * NO dynamic allocation to avoid complexity
 */
uint64_t *paging_existing_table(uint64_t *table, size_t index)
{
    if (!mm_pte_present(table[index]))
        return NULL;

    if (paging_entry_large(table[index]))
        return NULL;

    return paging_entry_table(table[index]);
}

/**
 * Allocate a new page table if it doesn't exist
 * Returns physical address of the table, or 0 on failure
 */
static uint64_t alloc_page_table(int level)
{
    uintptr_t frame = page_table_frame_alloc(MM_ALLOC_ATOMIC);
    void *page = (void *)frame;
    paging_oom_class_t oom_cls;

    if (!page)
    {
        oom_cls = paging_oom_classify_current();
        paging_oom_note("alloc_page_table", oom_cls);
        return 0;
    }
    
    /* Zero out the page */
    memset(page, 0, PAGE_SIZE_4KB);

    switch (level)
    {
    case 1:
        ir0_mm_level1_created++;
        break;
    case 2:
        ir0_mm_level2_created++;
        break;
    case 3:
        ir0_mm_level3_created++;
        break;
    default:
        break;
    }
    ir0_mm_set_frame_type((uint64_t)(uintptr_t)page, IR0_MM_FRAME_PT);
    ir0_mm_frame_pt_alloc++;
    ir0_mm_log_frame_type("ALLOC", (uint64_t)(uintptr_t)page, IR0_MM_FRAME_PT);

    return (uint64_t)frame;
}

/*
 * Split a 2 MiB PDE to 512×4 KiB leaves so a USER PTE can be installed.
 *
 * Linux huge-break: untouched slots are pte_none. Below PMM_PHYS_BASE keep
 * supervisor identity (kmalloc). In the PMM window use pte_none — kstacks
 * no longer live there (IR0_KSTACK_VA_BASE). Phys COW/zero: boot address-space root.
 */
static int paging_split_large_level2_leaf(address_space_root_t root,
					  uint64_t *level2_table, size_t index)
{
	uint64_t level2_entry;
	uint64_t phys_base;
	uint64_t pt_phys;
	uint64_t *level3_table;
	uint64_t leaf_flags;
	int exec;
	size_t i;

	if (!level2_table)
		return -1;
	level2_entry = level2_table[index];
	if (!mm_pte_present(level2_entry) || !paging_entry_large(level2_entry))
		return -1;

	pt_phys = alloc_page_table(3);
	if (pt_phys == 0)
		return -1;
	level3_table = (uint64_t *)(uintptr_t)pt_phys;
	phys_base = paging_entry_pfn(level2_entry);
	leaf_flags = PAGE_RW;
	exec = mm_pte_executable(level2_entry);

	for (i = 0; i < mm_table_entries(); i++)
	{
		uintptr_t pa = phys_base + (i * PAGE_SIZE_4KB);

		if (pa >= (uintptr_t)PMM_PHYS_BASE)
			level3_table[i] = 0;
		else
			level3_table[i] = mm_make_leaf_pte(pa, leaf_flags, exec);
	}

	level2_table[index] = mm_make_table_pte((uintptr_t)pt_phys, 0);
	mm_tlb_sync_all(root);
	return 0;
}

/*
 * Ensure @vaddr is addressable via a 4KiB PTE (split a PD-level 2MiB leaf if
 * needed). Does not allocate a missing leaf or change an existing 4KiB PTE.
 * Returns 0 if paging_get_pte can proceed, -1 on failure.
 */
int paging_ensure_4k_leaf(address_space_root_t root, uintptr_t vaddr)
{
	size_t idx[4];
	uint64_t *level1_table;
	uint64_t *level2_table;

	if (!root)
		return -1;

	mm_va_indices(vaddr, idx);

	if (!mm_pte_present(root[idx[0]]) || paging_entry_large(root[idx[0]]))
		return -1;
	level1_table = paging_entry_table(root[idx[0]]);
	if (!level1_table || !mm_pte_present(level1_table[idx[1]]) ||
	    paging_entry_large(level1_table[idx[1]]))
		return -1;
	level2_table = paging_entry_table(level1_table[idx[1]]);
	if (!level2_table || !mm_pte_present(level2_table[idx[2]]))
		return -1;
	if (paging_entry_large(level2_table[idx[2]]))
	{
		if (paging_split_large_level2_leaf(root, level2_table, idx[2]) != 0)
			return -1;
	}
	return 0;
}

/**
 * Get or create a page table at the specified level
 * @root: address-space root table address
 * @index: Index into the table
 * @create: If 1, create the table if it doesn't exist
 * Returns: Virtual address of the table (NULL if not present and create=0)
 */
uint64_t *paging_get_or_create_table(address_space_root_t root,
				     uint64_t *parent, size_t index, int create,
                                     uint64_t map_flags, int level)
{
	int user = !!(map_flags & PAGE_USER);

	if (!mm_pte_present(parent[index]))
	{
		uint64_t phys_addr;

		if (!create)
			return NULL;

		phys_addr = alloc_page_table(level);
		if (phys_addr == 0)
			return NULL;

		/*
		 * Linux: table entries use _KERNPG_TABLE / pgtable flags only — never
		 * NX on non-leaf levels.  Encode via mm_make_table_pte.
		 */
		parent[index] = mm_make_table_pte((uintptr_t)phys_addr, user);
		return (uint64_t *)paging_entry_pfn(phys_addr);
	}

	if (paging_entry_large(parent[index]))
	{
		if (!create || level != 3)
			return NULL;
		if (paging_split_large_level2_leaf(root, parent, index) != 0)
			return NULL;
	}

	/*
	 * Propagate PAGE_USER onto existing table levels (e.g. supervisor identity
	 * map created level1_table/level2_table/level3_table without U/S).  Linux requires user at all levels.
	 */
	if (user)
		mm_pte_set_user(&parent[index]);

	return paging_entry_table(parent[index]);
}

/**
 * Map a single 4KB page in a specific page directory
 * @root: address-space root table address (page directory)
 * @virt_addr: Virtual address to map
 * @phys_addr: Physical address to map to
 * @flags: Page flags (PAGE_USER, PAGE_RW, etc.)
 * Returns: 0 on success, -1 on failure
 */
int map_page_in_directory(address_space_root_t root, uint64_t virt_addr,
			  uint64_t phys_addr, uint64_t flags)
{
	size_t idx[4];
	uint64_t *level1_table;
	uint64_t *level2_table;
	uint64_t *level3_table;

	if (!root)
		return -1;

	/*
	 * USER maps over the supervisor identity window are allowed: brk growth
	 * and ELF BSS split 2MiB leaves via paging_split_large_level2_leaf.
	 */

	mm_va_indices((uintptr_t)virt_addr, idx);

	level1_table = paging_get_or_create_table(root, root, idx[0], 1, flags, 1);
	if (!level1_table)
		return -1;

	level2_table = paging_get_or_create_table(root, level1_table, idx[1], 1, flags, 2);
	if (!level2_table)
		return -1;

	level3_table = paging_get_or_create_table(root, level2_table, idx[2], 1, flags, 3);
	if (!level3_table)
		return -1;

	{
		uint64_t entry;
		uint64_t old_entry;
		uint64_t old_phys;
		int had_leaf;

		old_entry = level3_table[idx[3]];
		had_leaf = mm_pte_present(old_entry);
		old_phys = had_leaf ? paging_entry_pfn(old_entry) : 0;
		entry = mm_make_leaf_pte((uintptr_t)phys_addr, flags & PAGE_MAP_FLAGS_MASK,
					     !!(flags & PAGE_EXEC));
		level3_table[idx[3]] = entry;
		/*
		 * Replacing a different PMM frame must drop the old one.
		 * mprotect rewrites the same frame and must not.
		 */
		if (had_leaf && old_phys != paging_entry_pfn(entry) &&
		    old_phys >= pmm_get_start() && old_phys < pmm_get_end())
			pmm_frame_put(old_phys);
		if (!had_leaf)
			ir0_mm_leaf_created++;

		if (flags & PAGE_USER)
		{
			ir0_mm_set_frame_type(phys_addr, IR0_MM_FRAME_USER);
			ir0_mm_frame_user_alloc++;
			ir0_mm_log_frame_type("ALLOC", phys_addr, IR0_MM_FRAME_USER);
		}
		else
		{
			ir0_mm_set_frame_type(phys_addr, IR0_MM_FRAME_KERNEL);
			ir0_mm_frame_kernel_alloc++;
			ir0_mm_log_frame_type("ALLOC", phys_addr, IR0_MM_FRAME_KERNEL);
		}
	}

	/* Flush TLB — skip invlpg: mapping runs under kernel address-space root and foreign
	 * user VAs may be absent from the active page tables; address-space root reload on
	 * context switch flushes user TLB entries anyway.
	 */

	return 0;
}

int paging_install_pte(address_space_root_t root, uintptr_t virt_addr, uint64_t pte)
{
	size_t idx[4];
	uint64_t *level1_table;
	uint64_t *level2_table;
	uint64_t *level3_table;
	uint64_t flags = PAGE_USER;

	if (!root)
		return -1;
	mm_va_indices(virt_addr, idx);
	level1_table = paging_get_or_create_table(root, root, idx[0], 1, flags, 1);
	if (!level1_table)
		return -1;
	level2_table = paging_get_or_create_table(root, level1_table, idx[1], 1, flags, 2);
	if (!level2_table)
		return -1;
	level3_table = paging_get_or_create_table(root, level2_table, idx[2], 1, flags, 3);
	if (!level3_table)
		return -1;
	level3_table[idx[3]] = pte;
	return 0;
}

/**
 * Map a single 4KB page - SIMPLIFIED
 * Only works with existing page tables from boot
 * NO dynamic allocation (uses current address-space root)
 */
int map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags)
{
    /* Get current address-space root (address-space root address) */
    address_space_root_t root =
        (address_space_root_t)paging_current_address_space();

    return map_page_in_directory(root, virt_addr, phys_addr, flags);
}

int protect_user_region_in_directory(address_space_root_t root,
				     uintptr_t start, size_t length, uint64_t flags)
{
	uintptr_t end;
	uintptr_t page;

	if (!root || length == 0 || (start & (PAGE_SIZE_4KB - 1U)) != 0)
		return -1;
	if (length > UINTPTR_MAX - (PAGE_SIZE_4KB - 1U))
		return -1;
	length = (length + PAGE_SIZE_4KB - 1U) & ~(PAGE_SIZE_4KB - 1U);
	if (start > UINTPTR_MAX - length)
		return -1;
	end = start + length;
	flags |= PAGE_USER;

	for (page = start; page < end; page += PAGE_SIZE_4KB)
	{
		uint64_t *pte = paging_get_pte(root, page);
		uint64_t page_flags = flags;

		if (!pte || !mm_pte_present(*pte))
			return -1;
		/* A shared COW frame must remain hardware read-only until its write fault. */
		if (pte_cow(*pte) && (page_flags & PAGE_RW))
		{
			page_flags &= ~PAGE_RW;
			page_flags |= PAGE_COW;
		}
		*pte = mm_make_leaf_pte(mm_pte_phys(*pte), page_flags,
					!!(page_flags & PAGE_EXEC));
		mm_tlb_sync_page(root, page);
	}
	return 0;
}

paging_unmap_test_hook_fn paging_unmap_test_hook;

/**
 * Unmap a single 4KB page in the page directory rooted at @root.
 * Safe when address-space root points at a different address space than @root.
 */
int unmap_page_in_directory(address_space_root_t root, uintptr_t virt_addr)
{
	size_t idx[4];
	uint64_t *level1_table;
	uint64_t *level2_table;
	uint64_t *level3_table;
	uint64_t phys_frame;
	uint64_t pt_frame;
	uint64_t pd_frame;
	uint64_t level1_frame;
	ir0_mm_frame_type_t freed_type;
	int split;

	if (!root)
		return -1;
	split = 0;
	if (paging_unmap_test_hook)
	{
		int hook_result = paging_unmap_test_hook(root, virt_addr);

		if (hook_result != -EOPNOTSUPP)
			return hook_result;
	}

	mm_va_indices(virt_addr, idx);

	if (!mm_pte_present(root[idx[0]]))
		return -1;
	if (paging_entry_large(root[idx[0]]))
		return -1;
	level1_table = paging_entry_table(root[idx[0]]);
	if (!level1_table || !mm_pte_present(level1_table[idx[1]]))
		return -1;
	if (paging_entry_large(level1_table[idx[1]]))
		return -1;
	level2_table = paging_entry_table(level1_table[idx[1]]);
	if (!level2_table || !mm_pte_present(level2_table[idx[2]]))
		return -1;
	if (paging_entry_large(level2_table[idx[2]]))
	{
		/*
		 * A 2 MiB leaf covers this 4 KiB address. Split it, then drop
		 * only the requested page. munmap and MAP_FIXED used to ignore
		 * the failure and leave the huge leaf in place.
		 */
		if (paging_ensure_4k_leaf(root, virt_addr) != 0)
			return -1;
		split = 1;
		level2_table = paging_entry_table(level1_table[idx[1]]);
		if (!level2_table || paging_entry_large(level2_table[idx[2]]))
			return -1;
	}
	level3_table = paging_entry_table(level2_table[idx[2]]);
	if (!level3_table)
		return -1;

	if (!mm_pte_present(level3_table[idx[3]]))
	{
		if (mm_pte_is_swap(level3_table[idx[3]]))
		{
			(void)ir0_swap_drop_pte(level3_table[idx[3]]);
			level3_table[idx[3]] = 0;
			mm_tlb_sync_page(root, (uintptr_t)virt_addr);
			return 0;
		}
		/* The 2 MiB split already dropped this slot. A hole stays -1. */
		return split ? 0 : -1;
	}

	phys_frame = paging_entry_pfn(level3_table[idx[3]]);
	level3_table[idx[3]] = 0;
    ir0_mm_leaf_freed++;

    mm_tlb_sync_page(root, (uintptr_t)virt_addr);

    /*
     * Only return frames that the PMM allocated from RAM. MMIO and other
     * identity-mapped device pages must not touch the bitmap.
     */
    if (phys_frame &&
        phys_frame >= pmm_get_start() && phys_frame < pmm_get_end())
    {
		if (pmm_frame_refcount(phys_frame) == 1)
		{
			freed_type = ir0_mm_get_frame_type(phys_frame);
			if (freed_type == IR0_MM_FRAME_USER)
				ir0_mm_frame_user_free++;
			else if (freed_type == IR0_MM_FRAME_KERNEL)
				ir0_mm_frame_kernel_free++;
			else if (freed_type == IR0_MM_FRAME_PT)
				ir0_mm_frame_pt_free++;
			ir0_mm_set_frame_type(phys_frame, IR0_MM_FRAME_UNKNOWN);
			ir0_mm_log_frame_type("FREE", phys_frame, freed_type);
		}
		pmm_frame_put(phys_frame);
    }

	if (page_table_is_empty(level3_table))
	{
		pt_frame = paging_entry_pfn(level2_table[idx[2]]);
		level2_table[idx[2]] = 0;
		ir0_mm_free_table_frame(pt_frame, 3);
	}

	if (page_table_is_empty(level2_table))
	{
		pd_frame = paging_entry_pfn(level1_table[idx[1]]);
		level1_table[idx[1]] = 0;
		ir0_mm_free_table_frame(pd_frame, 2);
	}

	if (mm_user_root_slot((unsigned)idx[0]) &&
	    page_table_is_empty(level1_table))
	{
		level1_frame = paging_entry_pfn(root[idx[0]]);
		root[idx[0]] = 0;
		ir0_mm_free_table_frame(level1_frame, 1);
	}

	return 0;
}

/**
 * Unmap a single 4KB page in the current address space
 */
int unmap_page(uint64_t virt_addr)
{
    address_space_root_t root =
        (address_space_root_t)paging_current_address_space();

    return unmap_page_in_directory(root, (uintptr_t)virt_addr);
}


/* Map user page with U/S=1 permissions */
int map_user_page(uintptr_t virtual_addr, uintptr_t physical_addr, uint64_t flags)
{
    /* Add user flag (U/S=1) */
    flags |= PAGE_USER;

    /* Map the page */
    return map_page(virtual_addr, physical_addr, flags);
}

/* Map user memory region in current page directory */
int map_user_region(uintptr_t virtual_start, size_t size, uint64_t flags)
{
    address_space_root_t root =
        (address_space_root_t)paging_current_address_space();
    return map_user_region_in_directory(root, virtual_start, size, flags);
}

/* Map user memory region in a specific page directory */
int map_user_region_in_directory(address_space_root_t root,
				 uintptr_t virtual_start, size_t size,
				 uint64_t flags)
{
	if (!root)
		return -1;
	if (size == 0)
		return 0;
	if (size > SIZE_MAX - (PAGE_SIZE_4KB - 1U))
		return -1;

    /* Align to 4KB */
    virtual_start &= (uintptr_t)PAGE_FRAME_MASK;
	if (virtual_start > UINTPTR_MAX - size)
		return -1;
    size = (size + (PAGE_SIZE_4KB - 1U)) & (size_t)PAGE_FRAME_MASK;
	if (size == 0 || virtual_start > UINTPTR_MAX - size)
		return -1;

    /* Add user flag */
    flags |= PAGE_USER;

    /* Map each page; on failure, rollback all mappings we made */
    for (size_t offset = 0; offset < size; offset += PAGE_SIZE_4KB)
    {
        uintptr_t virt_addr = virtual_start + offset;

        uintptr_t phys_addr = mm_alloc_frame(MM_ALLOC_DIRECT_RECLAIM);
        if (phys_addr == 0)
        {
            /* Rollback: unmap everything we mapped so far */
            for (size_t rb = 0; rb < offset; rb += PAGE_SIZE_4KB)
                unmap_page_in_directory(root, virtual_start + rb);
            return -1;
        }

        /* Linux: clear_highpage() before exposing anonymous user pages */
        paging_zero_phys_page(phys_addr);

        if (map_page_in_directory(root, virt_addr, phys_addr, flags) != 0)
        {
            pmm_free_frame(phys_addr);
            for (size_t rb = 0; rb < offset; rb += PAGE_SIZE_4KB)
                unmap_page_in_directory(root, virtual_start + rb);
            return -1;
        }
    }

    return 0;
}

