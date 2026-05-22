/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: paging.c
 * Description: IR0 kernel source/header file
 */

#include <stdint.h>
#include <ir0/logging.h>
#include <ir0/oops.h>
#include <ir0/process.h>
#include "paging.h"
#include <string.h>
#include <mm/allocator.h>
#include <ir0/kmem.h>
#include <mm/pmm.h>
#include <ir0/validation.h>

/*
 * Linux mm/x86: pte_pfn(), pud_page() — extract the physical pointer from a
 * descriptor without software/NX bits (PTE_PFN_MASK).
 */
static inline uintptr_t paging_entry_pfn(uint64_t entry)
{
    return (uintptr_t)(entry & PAGE_PTE_PFN_MASK);
}

static inline int paging_entry_large(uint64_t entry)
{
    return (entry & PAGE_PRESENT) && (entry & PAGE_SIZE_2MB_FLAG);
}

static inline uint64_t *paging_entry_table(uint64_t entry)
{
    if (!(entry & PAGE_PRESENT))
        return NULL;
    if (paging_entry_large(entry))
        return NULL;
    return (uint64_t *)paging_entry_pfn(entry);
}

void enable_paging(void)
{
    uint64_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    /* PG and WP: supervisor respects read-only PTEs */
    cr0 |= CR0_PG | CR0_WP;
    asm volatile("mov %0, %%cr0" ::"r"(cr0));
}

void setup_and_enable_paging(void)
{
    /* SILENT safety checks */
    /* DO NOT use print/log during critical setup */

    /* 1. Verify we're in 64-bit mode */
    uint64_t cr0, cr4;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    asm volatile("mov %%cr4, %0" : "=r"(cr4));

    if (!(cr4 & CR4_PAE))
    {
        /* PAE not enabled - critical failure, will triple fault */
        return;
    }

    /* Verify paging is enabled */
    if (!is_paging_enabled())
    {
        /* Paging not enabled - enable it */
        enable_paging();
    }
}

void load_page_directory(uint64_t pml4_addr)
{
    asm volatile("mov %0, %%cr3" ::"r"(pml4_addr));
}

uint64_t get_current_page_directory(void)
{
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

int is_paging_enabled(void)
{
    uint64_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    return (cr0 & CR0_PG) != 0;
}

/**
 * Check if a virtual address is mapped in a page directory
 * @pml4: PML4 table address (page directory)
 * @virt_addr: Virtual address to check
 * @flags_out: Optional output for page flags (can be NULL)
 * Returns: 1 if mapped, 0 if not mapped, -1 on error
 */
int is_page_mapped_in_directory(uint64_t *pml4, uint64_t virt_addr, uint64_t *flags_out)
{
    if (!pml4)
        return -1;
    
    /* Extract indices from virtual address */
    size_t pml4_index = (virt_addr >> 39) & PAGE_INDEX_MASK;
    size_t pdpt_index = (virt_addr >> 30) & PAGE_INDEX_MASK;
    size_t pd_index = (virt_addr >> 21) & PAGE_INDEX_MASK;
    size_t pt_index = (virt_addr >> 12) & PAGE_INDEX_MASK;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;

    if (!(pml4[pml4_index] & PAGE_PRESENT))
        return 0;
    if (paging_entry_large(pml4[pml4_index]))
        return 0;

    pdpt = paging_entry_table(pml4[pml4_index]);
    if (!pdpt || !(pdpt[pdpt_index] & PAGE_PRESENT))
        return 0;
    if (paging_entry_large(pdpt[pdpt_index]))
        return 0;

    pd = paging_entry_table(pdpt[pdpt_index]);
    if (!pd || !(pd[pd_index] & PAGE_PRESENT))
        return 0;
    if (paging_entry_large(pd[pd_index]))
        return 0;

    pt = paging_entry_table(pd[pd_index]);
    if (!pt || !(pt[pt_index] & PAGE_PRESENT))
        return 0;
    
    /* Page is mapped */
    if (flags_out)
        *flags_out = pt[pt_index] & 0xFFF;
    
    return 1;
}

uint64_t *paging_get_pte(uint64_t *pml4, uintptr_t vaddr)
{
    size_t pml4_index;
    size_t pdpt_index;
    size_t pd_index;
    size_t pt_index;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;

    if (!pml4)
        return NULL;

    pml4_index = ((uint64_t)vaddr >> 39) & PAGE_INDEX_MASK;
    pdpt_index = ((uint64_t)vaddr >> 30) & PAGE_INDEX_MASK;
    pd_index = ((uint64_t)vaddr >> 21) & PAGE_INDEX_MASK;
    pt_index = ((uint64_t)vaddr >> 12) & PAGE_INDEX_MASK;

    if (!(pml4[pml4_index] & PAGE_PRESENT))
        return NULL;
    if (paging_entry_large(pml4[pml4_index]))
        return NULL;

    pdpt = paging_entry_table(pml4[pml4_index]);
    if (!pdpt || !(pdpt[pdpt_index] & PAGE_PRESENT))
        return NULL;
    if (paging_entry_large(pdpt[pdpt_index]))
        return NULL;

    pd = paging_entry_table(pdpt[pdpt_index]);
    if (!pd || !(pd[pd_index] & PAGE_PRESENT))
        return NULL;
    if (paging_entry_large(pd[pd_index]))
        return NULL;

    pt = paging_entry_table(pd[pd_index]);
    if (!pt)
        return NULL;
    return &pt[pt_index];
}

/*
 * Copy or zero user memory via mapped physical frames while kernel CR3 is
 * active.  PMM-backed user pages are identity-mapped in the kernel address
 * space (see boot pd_minimal and PMM_PHYS_BASE).
 */

/*
 * FASE 23: snapshot of the last copy_to_user_region_in_directory attempt.
 *   [0] dst (initial)        [4] last page being processed
 *   [1] n   (initial)        [5] last pte present (0/1)
 *   [2] dst + n              [6] last phys frame address
 *   [3] pml4                 [7] call sequence number
 */
uint64_t fase23_copy_region_probe[8];
static uint64_t fase23_copy_region_seq;

int copy_to_user_region_in_directory(uint64_t *pml4, uintptr_t dst,
                                     const void *src, size_t n)
{
    const uint8_t *s = src;
    uintptr_t dst0 = dst;
    size_t n0 = n;

    fase23_copy_region_probe[0] = (uint64_t)dst0;
    fase23_copy_region_probe[1] = (uint64_t)n0;
    fase23_copy_region_probe[2] = (uint64_t)(dst0 + n0);
    fase23_copy_region_probe[3] = (uint64_t)(uintptr_t)pml4;
    fase23_copy_region_probe[7] = ++fase23_copy_region_seq;

    if (!pml4 || !src)
        return -1;

    while (n > 0)
    {
        uintptr_t page = dst & (uintptr_t)PAGE_FRAME_MASK;
        uint64_t *pte = paging_get_pte(pml4, page);
        uintptr_t phys;
        size_t off;
        size_t chunk;

        fase23_copy_region_probe[4] = (uint64_t)page;
        fase23_copy_region_probe[5] = (pte && (*pte & PAGE_PRESENT)) ? 1ULL : 0ULL;

        if (!pte || !(*pte & PAGE_PRESENT))
            return -1;

        phys = paging_entry_pfn(*pte);
        fase23_copy_region_probe[6] = (uint64_t)phys;
        off = dst & 0xFFFU;
        chunk = (size_t)(0x1000U - off);
        if (chunk > n)
            chunk = n;

        memcpy((void *)(phys + off), s, chunk);
        dst += chunk;
        s += chunk;
        n -= chunk;
    }

    return 0;
}

int zero_user_region_in_directory(uint64_t *pml4, uintptr_t dst, size_t n)
{
    if (!pml4)
        return -1;

    while (n > 0)
    {
        uintptr_t page = dst & (uintptr_t)PAGE_FRAME_MASK;
        uint64_t *pte = paging_get_pte(pml4, page);
        uintptr_t phys;
        size_t off;
        size_t chunk;

        if (!pte || !(*pte & PAGE_PRESENT))
            return -1;

        phys = paging_entry_pfn(*pte);
        off = dst & 0xFFFU;
        chunk = (size_t)(0x1000U - off);
        if (chunk > n)
            chunk = n;

        memset((void *)(phys + off), 0, chunk);
        dst += chunk;
        n -= chunk;
    }

    return 0;
}

/**
 * Simple page table getter - only returns existing tables
 * NO dynamic allocation to avoid complexity
 */
static uint64_t *get_existing_table(uint64_t *table, size_t index)
{
    if (!(table[index] & PAGE_PRESENT))
        return NULL;

    if (paging_entry_large(table[index]))
        return NULL;

    return paging_entry_table(table[index]);
}

/**
 * Allocate a new page table if it doesn't exist
 * Returns physical address of the table, or 0 on failure
 */
static uint64_t alloc_page_table(void)
{
    void *page = kmalloc_aligned(4096, 4096);
    if (!page)
        return 0;
    
    /* Zero out the page */
    memset(page, 0, 4096);
    
    /* Return physical address (identity mapped for now) */
    return (uint64_t)page;
}

/**
 * Get or create a page table at the specified level
 * @pml4: PML4 table address
 * @index: Index into the table
 * @create: If 1, create the table if it doesn't exist
 * Returns: Virtual address of the table (NULL if not present and create=0)
 */
static uint64_t *get_or_create_table(uint64_t *parent, size_t index, int create,
                                     uint64_t map_flags)
{
    uint64_t table_prot = PAGE_PRESENT | PAGE_RW;

    if (map_flags & PAGE_USER)
        table_prot |= PAGE_USER;

    if (!(parent[index] & PAGE_PRESENT))
    {
        uint64_t phys_addr;

        if (!create)
            return NULL;

        phys_addr = alloc_page_table();
        if (phys_addr == 0)
            return NULL;

        /*
         * Linux: table entries use _KERNPG_TABLE / pgtable flags only — never
         * NX on non-leaf levels.  PFN via PTE_PFN_MASK.
         */
        parent[index] = paging_entry_pfn(phys_addr) | table_prot;
        return (uint64_t *)paging_entry_pfn(phys_addr);
    }

    if (paging_entry_large(parent[index]))
        return NULL;

    /*
     * Propagate PAGE_USER onto existing table levels (e.g. supervisor identity
     * map created pdpt/pd/pt without U/S).  Linux requires user at all levels.
     */
    if (map_flags & PAGE_USER)
        parent[index] |= PAGE_USER;

    return paging_entry_table(parent[index]);
}

/**
 * Map a single 4KB page in a specific page directory
 * @pml4: PML4 table address (page directory)
 * @virt_addr: Virtual address to map
 * @phys_addr: Physical address to map to
 * @flags: Page flags (PAGE_USER, PAGE_RW, etc.)
 * Returns: 0 on success, -1 on failure
 */
int map_page_in_directory(uint64_t *pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags)
{
    if (!pml4)
        return -1;
    
    /* Extract indices from virtual address */
    size_t pml4_index = (virt_addr >> 39) & PAGE_INDEX_MASK;
    size_t pdpt_index = (virt_addr >> 30) & PAGE_INDEX_MASK;
    size_t pd_index = (virt_addr >> 21) & PAGE_INDEX_MASK;
    size_t pt_index = (virt_addr >> 12) & PAGE_INDEX_MASK;

    /* Get or create PDPT */
    uint64_t *pdpt = get_or_create_table(pml4, pml4_index, 1, flags);
    if (!pdpt)
        return -1;

    /* Get or create PD */
    uint64_t *pd = get_or_create_table(pdpt, pdpt_index, 1, flags);
    if (!pd)
        return -1;

    /* Get or create PT */
    uint64_t *pt = get_or_create_table(pd, pd_index, 1, flags);
    if (!pt)
        return -1;

    /* Map the page: low flags from caller; NX unless explicitly executable */
    {
        uint64_t entry;

        entry = paging_entry_pfn(phys_addr) | (flags & 0xFFF) | PAGE_PRESENT;
        /*
         * Data and other non-code mappings: RW without PAGE_EXEC, or any mapping
         * without PAGE_EXEC, get NX. Code paths pass PAGE_EXEC (e.g. ELF PF_X).
         */
        if (!(flags & PAGE_EXEC))
            entry |= PAGE_NX;
        pt[pt_index] = entry;
    }

    /* Flush TLB — skip invlpg: mapping runs under kernel CR3 and foreign
     * user VAs may be absent from the active page tables; CR3 reload on
     * context switch flushes user TLB entries anyway.
     */

    return 0;
}

/**
 * Map a single 4KB page - SIMPLIFIED
 * Only works with existing page tables from boot
 * NO dynamic allocation (uses current CR3)
 */
int map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags)
{
    /* Get current CR3 (PML4 address) */
    uint64_t cr3 = get_current_page_directory();
    uint64_t *pml4 = (uint64_t *)cr3;
    
    return map_page_in_directory(pml4, virt_addr, phys_addr, flags);
}

/**
 * Unmap a single 4KB page in the page directory rooted at @pml4.
 * Safe when CR3 points at a different address space than @pml4.
 */
int unmap_page_in_directory(uint64_t *pml4, uintptr_t virt_addr)
{
    size_t pml4_index;
    size_t pdpt_index;
    size_t pd_index;
    size_t pt_index;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    uint64_t phys_frame;

    if (!pml4)
        return -1;

    pml4_index = ((uintptr_t)virt_addr >> 39) & PAGE_INDEX_MASK;
    pdpt_index = ((uintptr_t)virt_addr >> 30) & PAGE_INDEX_MASK;
    pd_index = ((uintptr_t)virt_addr >> 21) & PAGE_INDEX_MASK;
    pt_index = ((uintptr_t)virt_addr >> 12) & PAGE_INDEX_MASK;

    if (!(pml4[pml4_index] & PAGE_PRESENT))
        return -1;
    if (paging_entry_large(pml4[pml4_index]))
        return -1;
    pdpt = paging_entry_table(pml4[pml4_index]);
    if (!pdpt || !(pdpt[pdpt_index] & PAGE_PRESENT))
        return -1;
    if (paging_entry_large(pdpt[pdpt_index]))
        return -1;
    pd = paging_entry_table(pdpt[pdpt_index]);
    if (!pd || !(pd[pd_index] & PAGE_PRESENT))
        return -1;
    if (paging_entry_large(pd[pd_index]))
        return -1;
    pt = paging_entry_table(pd[pd_index]);
    if (!pt)
        return -1;

    phys_frame = paging_entry_pfn(pt[pt_index]);
    pt[pt_index] = 0;

    __asm__ volatile("invlpg (%0)" ::"r"(virt_addr) : "memory");

    /*
     * Only return frames that the PMM allocated from RAM. MMIO and other
     * identity-mapped device pages must not touch the bitmap.
     */
    if (phys_frame &&
        phys_frame >= pmm_get_start() && phys_frame < pmm_get_end())
        pmm_free_frame(phys_frame);

    return 0;
}

/**
 * Unmap a single 4KB page in the current address space
 */
int unmap_page(uint64_t virt_addr)
{
    uint64_t cr3 = get_current_page_directory();

    return unmap_page_in_directory((uint64_t *)cr3, (uintptr_t)virt_addr);
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
    uint64_t cr3 = get_current_page_directory();
    uint64_t *pml4 = (uint64_t *)cr3;
    return map_user_region_in_directory(pml4, virtual_start, size, flags);
}

/* Map user memory region in a specific page directory */
int map_user_region_in_directory(uint64_t *pml4, uintptr_t virtual_start, size_t size, uint64_t flags)
{
    if (!pml4)
        return -1;
    
    /* Align to 4KB */
    virtual_start &= (uintptr_t)PAGE_FRAME_MASK;
    size = (size + 0xFFF) & (size_t)PAGE_FRAME_MASK;

    /* Add user flag */
    flags |= PAGE_USER;

    /* Map each page; on failure, rollback all mappings we made */
    for (size_t offset = 0; offset < size; offset += 0x1000)
    {
        uintptr_t virt_addr = virtual_start + offset;

        uintptr_t phys_addr = pmm_alloc_frame();
        if (phys_addr == 0)
        {
            /* Rollback: unmap everything we mapped so far */
            for (size_t rb = 0; rb < offset; rb += 0x1000)
                unmap_page_in_directory(pml4, virtual_start + rb);
            return -1;
        }

        /* Linux: clear_highpage() before exposing anonymous user pages */
        memset((void *)phys_addr, 0, 4096);

        if (map_page_in_directory(pml4, virt_addr, phys_addr, flags) != 0)
        {
            pmm_free_frame(phys_addr);
            for (size_t rb = 0; rb < offset; rb += 0x1000)
                unmap_page_in_directory(pml4, virtual_start + rb);
            return -1;
        }
    }

    return 0;
}

/*
 * copy_process_memory - Copy memory from parent to child process
 * @parent: Parent process
 * @child: Child process
 *
 * Walks PML4 indices 0..255 (user half of the canonical 64-bit space), then
 * every present 4KB leaf with PAGE_USER in the parent. For each such page,
 * allocates a fresh frame, copies content (identity-mapped physical access),
 * and maps it in the child's PML4 via map_page_in_directory (no CR3 dance).
 *
 * Same 4KB-only assumption as process_unmap_user_pages_all: huge pages are
 * skipped. Kernel mappings (no PAGE_USER) are not copied. No COW.
 *
 * Returns: 0 on success, -1 on failure
 */
int copy_process_memory(struct process *parent, struct process *child)
{
    size_t i4;
    size_t i3;
    size_t i2;
    size_t i1;
    uint64_t *parent_pml4;
    uint64_t *child_pml4;

    if (!parent || !child)
        return -1;

    if (!parent->page_directory || !child->page_directory)
        return -1;

    parent_pml4 = parent->page_directory;
    child_pml4 = child->page_directory;

    for (i4 = 0; i4 < 256; i4++)
    {
        uint64_t *pdpt = get_existing_table(parent_pml4, i4);

        if (!pdpt)
            continue;

        for (i3 = 0; i3 < 512; i3++)
        {
            uint64_t *pd = get_existing_table(pdpt, i3);

            if (!pd)
                continue;

            for (i2 = 0; i2 < 512; i2++)
            {
                uint64_t *pt = get_existing_table(pd, i2);

                if (!pt)
                    continue;

                for (i1 = 0; i1 < 512; i1++)
                {
                    uint64_t page_entry = pt[i1];
                    uint64_t parent_phys;
                    uint64_t child_phys;
                    uint64_t flags;
                    uintptr_t virt_addr;

                    if (!(page_entry & PAGE_PRESENT) ||
                        !(page_entry & PAGE_USER))
                        continue;

                    parent_phys = paging_entry_pfn(page_entry);

                    child_phys = pmm_alloc_frame();
                    if (!child_phys)
                        return -1;

                    virt_addr = ((uintptr_t)i4 << 39) |
                                ((uintptr_t)i3 << 30) |
                                ((uintptr_t)i2 << 21) |
                                ((uintptr_t)i1 << 12);

                    memcpy((void *)child_phys, (void *)parent_phys,
                           PAGE_SIZE_4KB);

                    flags = page_entry & 0xFFF;
                    flags &= ~PAGE_GLOBAL;
                    flags |= PAGE_USER;
                    if (!(page_entry & PAGE_NX))
                        flags |= PAGE_EXEC;

                    if (map_page_in_directory(child_pml4, virt_addr,
                                              child_phys, flags) != 0)
                    {
                        pmm_free_frame(child_phys);
                        return -1;
                    }
                }
            }
        }
    }

    return 0;
}

/*
 * map_supervisor_identity_low - 4 KiB identity map for kernel low memory
 *
 * Boot uses 2 MiB huge pages in PML4[0]; inheriting that tree blocks user
 * ELF at 0x400000. Each process instead gets fresh 4 KiB supervisor mappings
 * for the low identity range so timer IRQ (TSS RSP0) and syscall entry can
 * run with process CR3 loaded (Linux/BSD: kernel always reachable from user mm).
 */
int map_supervisor_identity_low(uint64_t *pml4, uint64_t start, uint64_t end)
{
    uint64_t va;

    if (!pml4 || end <= start)
        return -1;

    start &= ~(uint64_t)(PAGE_SIZE_4KB - 1);
    end = (end + PAGE_SIZE_4KB - 1) & ~(uint64_t)(PAGE_SIZE_4KB - 1);

    for (va = start; va < end; va += PAGE_SIZE_4KB)
    {
        /*
         * IRQ/syscall entry runs kernel text under process CR3; omit NX on
         * the identity map (Linux keeps kernel text executable in every mm).
         */
        if (map_page_in_directory(pml4, va, va,
                                  PAGE_PRESENT | PAGE_RW | PAGE_EXEC) != 0)
            return -1;
    }

    return 0;
}
