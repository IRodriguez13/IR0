#include <stdint.h>
#include <ir0/vga.h>
#include <ir0/logging.h>
#include <ir0/oops.h>
#include "paging.h"
#include <string.h>
#include <mm/allocator.h>
#include <kernel/process.h>
#include <ir0/kmem.h>
#include <mm/pmm.h>
#include <ir0/validation.h>

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
    size_t pml4_index = (virt_addr >> 39) & 0x1FF;
    size_t pdpt_index = (virt_addr >> 30) & 0x1FF;
    size_t pd_index = (virt_addr >> 21) & 0x1FF;
    size_t pt_index = (virt_addr >> 12) & 0x1FF;
    
    /* Walk page tables */
    if (!(pml4[pml4_index] & PAGE_PRESENT))
        return 0;
    
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_index] & ~0xFFF);
    if (!(pdpt[pdpt_index] & PAGE_PRESENT))
        return 0;
    
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_index] & ~0xFFF);
    if (!(pd[pd_index] & PAGE_PRESENT))
        return 0;
    
    uint64_t *pt = (uint64_t *)(pd[pd_index] & ~0xFFF);
    if (!(pt[pt_index] & PAGE_PRESENT))
        return 0;
    
    /* Page is mapped */
    if (flags_out)
        *flags_out = pt[pt_index] & 0xFFF;
    
    return 1;
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
 * Allocate a new page table if it doesn't exist
 * Returns physical address of the table, or 0 on failure
 */
static uint64_t alloc_page_table(void)
{
    void *page = kmalloc(4096);
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
static uint64_t *get_or_create_table(uint64_t *pml4, size_t index, int create)
{
    if (!(pml4[index] & PAGE_PRESENT))
    {
        if (!create)
            return NULL;
        
        /* Allocate new table */
        uint64_t phys_addr = alloc_page_table();
        if (phys_addr == 0)
            return NULL;
        
        /* Map the table */
        pml4[index] = phys_addr | PAGE_PRESENT | PAGE_RW;
        return (uint64_t *)phys_addr;  /* Identity mapped */
    }
    
    /* Check if it's a huge page */
    if (pml4[index] & (1ULL << 7))
        return NULL;
    
    /* Return physical address (identity mapped) */
    return (uint64_t *)(pml4[index] & ~0xFFF);
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
    size_t pml4_index = (virt_addr >> 39) & 0x1FF;
    size_t pdpt_index = (virt_addr >> 30) & 0x1FF;
    size_t pd_index = (virt_addr >> 21) & 0x1FF;
    size_t pt_index = (virt_addr >> 12) & 0x1FF;

    /* Get or create PDPT */
    uint64_t *pdpt = get_or_create_table(pml4, pml4_index, 1);
    if (!pdpt)
        return -1;

    /* Get or create PD */
    uint64_t *pd = get_or_create_table(pdpt, pdpt_index, 1);
    if (!pd)
        return -1;

    /* Get or create PT */
    uint64_t *pt = get_or_create_table(pd, pd_index, 1);
    if (!pt)
        return -1;

    /* Map the page */
    pt[pt_index] = (phys_addr & ~0xFFF) | (flags & 0xFFF) | PAGE_PRESENT;

    /* Flush TLB */
    __asm__ volatile("invlpg (%0)" ::"r"(virt_addr) : "memory");

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

        /* Map the page in the specified directory */
        if (map_page_in_directory(pml4, virt_addr, phys_addr, flags) != 0)
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
 * BASIC IMPLEMENTATION: Copies user-space pages from parent to child.
 * 
 * Implementation notes:
 * - Walks parent's page tables to find user pages (PAGE_USER flag)
 * - Allocates new frames for child using pmm_alloc_frame()
 * - Copies PAGE_SIZE_4KB bytes per page
 * - Maps pages in child's page directory
 * 
 * LIMITATIONS:
 * - Only handles user-space pages (kernel pages are shared)
 * - Assumes 4KB pages only (no 2MB pages)
 * - Does not handle Copy-on-Write (COW) - full copy performed
 * - No error recovery if frame allocation fails mid-copy
 * 
 * Returns: 0 on success, -1 on failure
 */
int copy_process_memory(struct process *parent, struct process *child)
{
    if (!parent || !child)
        return -1;
    
    if (!parent->page_directory || !child->page_directory)
        return -1;
    
    uint64_t *parent_pml4 = parent->page_directory;
    uint64_t *child_pml4 = child->page_directory;
    
    /* Walk parent's page tables to find user pages */
    /* User space typically starts at 0x400000 (4MB) in x86-64 */
    /* We scan a limited range: 0x400000 to 0x1000000 (4MB to 16MB) */
    const uint64_t USER_START = 0x400000;
    const uint64_t USER_END = 0x1000000;
    
    /* Temporarily switch to parent's page directory to read pages */
    uint64_t old_cr3 = get_current_page_directory();
    load_page_directory((uint64_t)parent_pml4);
    
    for (uint64_t virt_addr = USER_START; virt_addr < USER_END; virt_addr += PAGE_SIZE_4KB)
    {
        /* Extract page table indices */
        size_t pml4_index = (virt_addr >> 39) & 0x1FF;
        size_t pdpt_index = (virt_addr >> 30) & 0x1FF;
        size_t pd_index = (virt_addr >> 21) & 0x1FF;
        size_t pt_index = (virt_addr >> 12) & 0x1FF;
        
        /* Walk parent's page tables */
        if (!(parent_pml4[pml4_index] & PAGE_PRESENT))
            continue;
        
        uint64_t *pdpt = get_existing_table(parent_pml4, pml4_index);
        if (!pdpt)
            continue;
        
        if (!(pdpt[pdpt_index] & PAGE_PRESENT))
            continue;
        
        uint64_t *pd = get_existing_table(pdpt, pdpt_index);
        if (!pd)
            continue;
        
        if (!(pd[pd_index] & PAGE_PRESENT))
            continue;
        
        uint64_t *pt = get_existing_table(pd, pd_index);
        if (!pt)
            continue;
        
        /* Check if page is present and is user page */
        uint64_t page_entry = pt[pt_index];
        if (!(page_entry & PAGE_PRESENT))
            continue;
        
        if (!(page_entry & PAGE_USER))
            continue; /* Skip kernel pages */
        
        /* Get physical address of parent page */
        uint64_t parent_phys = page_entry & ~0xFFF;
        
        /* Allocate new frame for child */
        uint64_t child_phys = pmm_alloc_frame();
        if (!child_phys)
        {
            /* Restore CR3 and return error */
            load_page_directory(old_cr3);
            return -1;
        }
        
        /* Copy page content (identity mapped, so we can access physical directly) */
        void *parent_page = (void *)parent_phys;
        void *child_page = (void *)child_phys;
        memcpy(child_page, parent_page, PAGE_SIZE_4KB);
        
        /* Get page flags (preserve all except global flag) */
        uint64_t flags = page_entry & 0xFFF;
        flags &= ~PAGE_GLOBAL; /* Don't mark child pages as global */
        
        /* Switch to child's page directory and map the page */
        load_page_directory((uint64_t)child_pml4);
        
        /* Create page tables in child if needed (simplified - assumes they exist) */
        /* Map the copied page in child's address space */
        if (map_user_page(virt_addr, child_phys, flags) != 0)
        {
            pmm_free_frame(child_phys);
            load_page_directory(old_cr3);
            return -1;
        }
        
        /* Switch back to parent's directory for next iteration */
        load_page_directory((uint64_t)parent_pml4);
    }
    
    /* Restore original CR3 */
    load_page_directory(old_cr3);
    
    return 0;
}
