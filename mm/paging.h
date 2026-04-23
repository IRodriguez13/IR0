#pragma once

#include <stdint.h>
#include <stddef.h>


#define PAGE_PRESENT 0x1
#define PAGE_RW 0x2
#define PAGE_USER 0x4
#define PAGE_WRITETHROUGH 0x8
#define PAGE_CACHE_DISABLE 0x10
#define PAGE_ACCESSED 0x20
#define PAGE_DIRTY 0x40
#define PAGE_SIZE_2MB_FLAG 0x80
#define PAGE_GLOBAL 0x100

/* PTE bit 63: no-execute when IA32_EFER.NXE is set (not stored in low 12 bits of API flags) */
#define PAGE_NX (1ULL << 63)
/* Software flag for map_page*: page must remain executable (omit PAGE_NX in PTE) */
#define PAGE_EXEC (1ULL << 52)


#define PAGE_SIZE_4KB (4 * 1024)
#define PAGE_SIZE_2MB (2 * 1024 * 1024)
#define PAGE_SIZE_1GB (1024 * 1024 * 1024)

/* Physical frame: strip low 12 bits from a table entry or aligned address */
#define PAGE_FRAME_MASK     (~0xFFFULL)
/* Nine-bit index into PML4/PDPT/PD/PT (512 entries) */
#define PAGE_INDEX_MASK     0x1FF

/* CR0: paging and write-protect */
#define CR0_PG              (1UL << 31)
#define CR0_WP              (1UL << 16)
/* CR4: physical address extension (required before long-mode paging) */
#define CR4_PAE             (1UL << 5)


/**
 * Enable paging (sets CR0.PG bit)
 */
void enable_paging(void);

/**
 * Complete paging setup: configure tables and enable paging
 * This function handles all paging configuration from C code
 */
void setup_and_enable_paging(void);

/**
 * Load page directory into CR3
 */
void load_page_directory(uint64_t pml4_addr);

/**
 * Get current page directory address from CR3
 */
uint64_t get_current_page_directory(void);

/**
 * Check if paging is enabled
 */
int is_paging_enabled(void);

/**
 * Map a virtual address to a physical address
 * Note: This is a simple implementation for testing
 */
int map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);

/**
 * Map a virtual address to a physical address in a specific page directory
 * @pml4: PML4 table address (page directory)
 * @virt_addr: Virtual address to map
 * @phys_addr: Physical address to map to
 * @flags: Page flags (PAGE_USER, PAGE_RW, etc.)
 * Returns: 0 on success, -1 on failure
 */
int map_page_in_directory(uint64_t *pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);

/**
 * Check if a virtual address is mapped in a page directory
 * @pml4: PML4 table address (page directory)
 * @virt_addr: Virtual address to check
 * @flags_out: Optional output for page flags (can be NULL)
 * Returns: 1 if mapped, 0 if not mapped, -1 on error
 */
int is_page_mapped_in_directory(uint64_t *pml4, uint64_t virt_addr, uint64_t *flags_out);

/**
 * Walk 4-level paging to the PTE for @a vaddr in @a pml4.
 * Returns NULL if any level is missing or a huge page is encountered.
 * The returned pointer is valid even when the leaf PTE is not present.
 */
uint64_t *paging_get_pte(uint64_t *pml4, uintptr_t vaddr);

/**
 * Unmap a 4KB page in an explicit page directory (PML4 root).
 * Does not assume current CR3 matches @pml4.
 */
int unmap_page_in_directory(uint64_t *pml4, uintptr_t virt_addr);

/**
 * Unmap a virtual address in the current address space (current CR3)
 */
int unmap_page(uint64_t virt_addr);

/* Map user page with U/S=1 */
int map_user_page(uintptr_t virtual_addr, uintptr_t physical_addr, uint64_t flags);

/* Map user region in current page directory */
int map_user_region(uintptr_t virtual_start, size_t size, uint64_t flags);

/* Map user region in a specific page directory */
int map_user_region_in_directory(uint64_t *pml4, uintptr_t virtual_start, size_t size, uint64_t flags);

struct process;

/**
 * Create a new page directory for a user process
 * Returns physical address of the new PML4 table
 */
uint64_t create_process_page_directory(void);

/**
 * Copy memory from parent process to child process
 * Used for fork() implementation
 */
int copy_process_memory(struct process *parent, struct process *child);