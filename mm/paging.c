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
    
    /* Walk page tables */
    if (!(pml4[pml4_index] & PAGE_PRESENT))
        return 0;
    
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_index] & PAGE_FRAME_MASK);
    if (!(pdpt[pdpt_index] & PAGE_PRESENT))
        return 0;
    
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_index] & PAGE_FRAME_MASK);
    if (!(pd[pd_index] & PAGE_PRESENT))
        return 0;
    
    uint64_t *pt = (uint64_t *)(pd[pd_index] & PAGE_FRAME_MASK);
    if (!(pt[pt_index] & PAGE_PRESENT))
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
    if (pml4[pml4_index] & PAGE_SIZE_2MB_FLAG)
        return NULL;

    pdpt = (uint64_t *)(pml4[pml4_index] & PAGE_FRAME_MASK);
    if (!(pdpt[pdpt_index] & PAGE_PRESENT))
        return NULL;
    if (pdpt[pdpt_index] & PAGE_SIZE_2MB_FLAG)
        return NULL;

    pd = (uint64_t *)(pdpt[pdpt_index] & PAGE_FRAME_MASK);
    if (!(pd[pd_index] & PAGE_PRESENT))
        return NULL;
    if (pd[pd_index] & PAGE_SIZE_2MB_FLAG)
        return NULL;

    pt = (uint64_t *)(pd[pd_index] & PAGE_FRAME_MASK);
    return &pt[pt_index];
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
    return (uint64_t *)(table[index] & PAGE_FRAME_MASK);
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
    return (uint64_t *)(pml4[index] & PAGE_FRAME_MASK);
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

    /* Map the page: low flags from caller; NX unless explicitly executable */
    {
        uint64_t entry;

        entry = (phys_addr & PAGE_FRAME_MASK) | (flags & 0xFFF) | PAGE_PRESENT;
        /*
         * Data and other non-code mappings: RW without PAGE_EXEC, or any mapping
         * without PAGE_EXEC, get NX. Code paths pass PAGE_EXEC (e.g. ELF PF_X).
         */
        if (!(flags & PAGE_EXEC))
            entry |= PAGE_NX;
        pt[pt_index] = entry;
    }

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
    if (pml4[pml4_index] & PAGE_SIZE_2MB_FLAG)
        return -1;
    pdpt = (uint64_t *)(pml4[pml4_index] & PAGE_FRAME_MASK);

    if (!(pdpt[pdpt_index] & PAGE_PRESENT))
        return -1;
    if (pdpt[pdpt_index] & PAGE_SIZE_2MB_FLAG)
        return -1;
    pd = (uint64_t *)(pdpt[pdpt_index] & PAGE_FRAME_MASK);

    if (!(pd[pd_index] & PAGE_PRESENT))
        return -1;
    if (pd[pd_index] & PAGE_SIZE_2MB_FLAG)
        return -1;
    pt = (uint64_t *)(pd[pd_index] & PAGE_FRAME_MASK);

    phys_frame = pt[pt_index] & PAGE_FRAME_MASK;
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

                    parent_phys = page_entry & PAGE_FRAME_MASK;

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
