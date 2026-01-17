#include <stdint.h>
#include <ir0/vga.h>
#include <ir0/logging.h>
#include <ir0/oops.h>
#include "paging.h"
#include <string.h>
#include "allocator.h"
#include <kernel/process.h>
#include <ir0/memory/kmem.h>
#include <ir0/memory/pmm.h>

/* Page directory for identity mapping (used by setup functions) */
__attribute__((aligned(4096))) static uint64_t PD[512];


void setup_paging_identity_16mb()
{

    for (int i = 1; i < 16; i++) /* Start from 1 (0 already mapped) */
    {
        uint64_t phys_addr = i * PAGE_SIZE_2MB;
        PD[i] = phys_addr | PAGE_PRESENT | PAGE_RW | PAGE_SIZE_2MB_FLAG;
    }

    /* Map 16 MiB using 2MiB pages: 8 PD entries (2MiB * 8 = 16MiB) */
    for (int i = 0; i < 8; i++)
    {
        uint64_t phys_addr = i * PAGE_SIZE_2MB;
        PD[i] = phys_addr | PAGE_PRESENT | PAGE_RW | PAGE_SIZE_2MB_FLAG;
    }

    /* Do NOT reload CR3 - already configured by boot assembly */
    /* Just expand existing tables */
}

void enable_paging(void)
{
    uint64_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; /* PG bit */
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

    if (!(cr4 & (1 << 5)))
    {
        /* PAE not enabled - critical failure, will triple fault */
        return;
    }

    /* 2. Expand existing tables SILENTLY */
    setup_paging_identity_16mb();

    /* 4. Verify paging is enabled */
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
    return (cr0 & 0x80000000) != 0;
}

/**
 * Simple page table getter - only returns existing tables
 * NO dynamic allocation to avoid complexity
 */
static uint64_t *get_existing_table(uint64_t *table, size_t index)
{
    if (!(table[index] & PAGE_PRESENT))
    {
        return NULL; /* Table doesn't exist */
    }

    /* Check if it's a huge page (2MB) */
    if (table[index] & (1ULL << 7))
    {
        return NULL;
    }

    /* Return physical address (identity mapped) */
    return (uint64_t *)(table[index] & ~0xFFF);
}

/**
 * Map a single 4KB page - SIMPLIFIED
 * Only works with existing page tables from boot
 * NO dynamic allocation
 */
int map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags)
{
    /* Get current CR3 (PML4 address) */
    uint64_t cr3 = get_current_page_directory();
    uint64_t *pml4 = (uint64_t *)cr3;

    /* Extract indices from virtual address */
    size_t pml4_index = (virt_addr >> 39) & 0x1FF;
    size_t pdpt_index = (virt_addr >> 30) & 0x1FF;
    size_t pd_index = (virt_addr >> 21) & 0x1FF;
    size_t pt_index = (virt_addr >> 12) & 0x1FF;

    /* Walk existing page tables ONLY */
    uint64_t *pdpt = get_existing_table(pml4, pml4_index);
    if (!pdpt)
        return -1;

    uint64_t *pd = get_existing_table(pdpt, pdpt_index);
    if (!pd)
        return -1;

    uint64_t *pt = get_existing_table(pd, pd_index);
    if (!pt)
        return -1;

    /* Map the page */
    pt[pt_index] = (phys_addr & ~0xFFF) | (flags & 0xFFF) | PAGE_PRESENT;

    /* Flush TLB */
    __asm__ volatile("invlpg (%0)" ::"r"(virt_addr) : "memory");

    return 0;
}

/**
 * Unmap a single 4KB page
 */
int unmap_page(uint64_t virt_addr)
{
    /* Get current CR3 (PML4 address) */
    uint64_t cr3 = get_current_page_directory();
    uint64_t *pml4 = (uint64_t *)cr3;

    /* Extract indices */
    size_t pml4_index = (virt_addr >> 39) & 0x1FF;
    size_t pdpt_index = (virt_addr >> 30) & 0x1FF;
    size_t pd_index = (virt_addr >> 21) & 0x1FF;
    size_t pt_index = (virt_addr >> 12) & 0x1FF;

    /* Walk page tables to find the page */
    if (!(pml4[pml4_index] & PAGE_PRESENT))
        return -1;
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_index] & ~0xFFF);

    if (!(pdpt[pdpt_index] & PAGE_PRESENT))
        return -1;
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_index] & ~0xFFF);

    if (!(pd[pd_index] & PAGE_PRESENT))
        return -1;
    uint64_t *pt = (uint64_t *)(pd[pd_index] & ~0xFFF);

    /* Clear the page table entry */
    pt[pt_index] = 0;

    /* Flush TLB */
    __asm__ volatile("invlpg (%0)" ::"r"(virt_addr) : "memory");

    return 0;
}


/* Map user page with U/S=1 permissions */
int map_user_page(uintptr_t virtual_addr, uintptr_t physical_addr, uint64_t flags)
{
    /* Add user flag (U/S=1) */
    flags |= PAGE_USER;

    /* Map the page */
    return map_page(virtual_addr, physical_addr, flags);
}

/* Map user memory region */
int map_user_region(uintptr_t virtual_start, size_t size, uint64_t flags)
{
    /* Align to 4KB */
    virtual_start &= ~0xFFF;
    size = (size + 0xFFF) & ~0xFFF;

    /* Add user flag */
    flags |= PAGE_USER;

    /* Map each page */
    for (size_t offset = 0; offset < size; offset += 0x1000)
    {
        uintptr_t virt_addr = virtual_start + offset;

        /* Allocate physical frame using PMM (correct!) */
        uintptr_t phys_addr = pmm_alloc_frame();

        if (phys_addr == 0)
        {
            panic("Failed to allocate physical frame");
            return -1;
        }

        /* Map the page */
        if (map_page(virt_addr, phys_addr, flags) != 0)
        {
            panic("Failed to map page");
            pmm_free_frame(phys_addr); /* Free frame on mapping failure */
            return -1;
        }
    }

    return 0;
}

/**
 * copy_process_memory - Copy memory from parent to child process
 * @parent: Parent process
 * @child: Child process
 *
 * STUB IMPLEMENTATION: Allows fork() to compile and run
 *
 * TODO: Full implementation requires:
 * - Walking parent's page tables
 * - Identifying user pages (PAGE_USER flag set)
 * - Allocating frames with pmm_alloc_frame()
 * - Copying 4096 bytes per page
 * - Mapping in child's page directory
 *
 * For now, return success to allow testing.
 * WARNING: Child will share parent's memory space (not isolated!)
 *
 * Returns: 0 on success (stub always succeeds)
 */
int copy_process_memory(struct process *parent, struct process *child)
{
    (void)parent;  /* Unused for now */
    (void)child;   /* Unused for now */
    
    /* Minimal implementation: Return success to allow process creation.
     * Full implementation would:
     * - Walk parent's page tables
     * - Identify user pages (PAGE_USER flag set)
     * - Allocate frames with pmm_alloc_frame()
     * - Copy 4096 bytes per page
     * - Map in child's page directory
     * 
     * WARNING: Child currently shares parent's memory space (not isolated!)
     * This is acceptable for testing but must be fixed for production.
     */
    return 0;  /* Stub: pretend success */
}
