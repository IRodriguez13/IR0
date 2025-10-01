// ===============================================================================
// IR0 KERNEL - VIRTUAL MEMORY MANAGER
// ===============================================================================

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "paging_x64.h"

// ===============================================================================
// VIRTUAL MEMORY CONSTANTS
// ===============================================================================

// Virtual address space layout
#define KERNEL_VIRTUAL_BASE     0xFFFF800000000000ULL  // -128TB (kernel space)
#define USER_VIRTUAL_BASE       0x0000000000400000ULL  // 4MB (user space start)
#define USER_VIRTUAL_END        0x00007FFFFFFFFFFFULL  // 128TB (user space end)

// Virtual memory areas
#define VMA_KERNEL_CODE         0xFFFF800000000000ULL
#define VMA_KERNEL_DATA         0xFFFF800010000000ULL  
#define VMA_KERNEL_HEAP         0xFFFF800020000000ULL
#define VMA_KERNEL_MODULES      0xFFFF800030000000ULL

#define VMA_USER_CODE           0x0000000000400000ULL  // 4MB
#define VMA_USER_DATA           0x0000000010000000ULL  // 256MB
#define VMA_USER_HEAP           0x0000000020000000ULL  // 512MB
#define VMA_USER_STACK          0x00007FFF00000000ULL  // Near top of user space

// Page table levels
#define PML4_ENTRIES            512
#define PDPT_ENTRIES            512  
#define PD_ENTRIES              512
#define PT_ENTRIES              512

// ===============================================================================
// VIRTUAL MEMORY AREA (VMA) STRUCTURE
// ===============================================================================

typedef enum {
    VMA_TYPE_CODE,
    VMA_TYPE_DATA,
    VMA_TYPE_HEAP,
    VMA_TYPE_STACK,
    VMA_TYPE_MMAP,
    VMA_TYPE_SHARED
} vma_type_t;

typedef struct virtual_memory_area {
    uintptr_t start;                    // Start virtual address
    uintptr_t end;                      // End virtual address
    size_t size;                        // Size in bytes
    vma_type_t type;                    // Type of VMA
    uint32_t flags;                     // Protection flags
    
    struct virtual_memory_area *next;   // Linked list
    struct virtual_memory_area *prev;
} vma_t;

// VMA flags
#define VMA_FLAG_READ           0x01
#define VMA_FLAG_WRITE          0x02
#define VMA_FLAG_EXEC           0x04
#define VMA_FLAG_USER           0x08
#define VMA_FLAG_SHARED         0x10
#define VMA_FLAG_GROWSDOWN      0x20    // Stack grows down

// ===============================================================================
// ADDRESS SPACE STRUCTURE
// ===============================================================================

typedef struct address_space {
    uint64_t *pml4;                     // Page table root
    vma_t *vma_list;                    // List of VMAs
    uintptr_t heap_start;               // Heap start address
    uintptr_t heap_end;                 // Current heap end
    uintptr_t stack_start;              // Stack start address
    uintptr_t mmap_base;                // mmap base address
    
    // Statistics
    size_t total_pages;
    size_t resident_pages;
    size_t shared_pages;
    
    // Reference counting
    uint32_t ref_count;
} address_space_t;

// ===============================================================================
// VIRTUAL MEMORY MANAGER STRUCTURE
// ===============================================================================

typedef struct virtual_memory_manager {
    address_space_t *kernel_space;      // Kernel address space
    address_space_t *current_space;     // Current user address space
    
    // Global page tables (shared kernel mappings)
    uint64_t *kernel_pml4;
    uint64_t *kernel_pdpt;
    uint64_t *kernel_pd;
    
    // Statistics
    size_t total_virtual_pages;
    size_t mapped_virtual_pages;
    size_t page_faults;
    size_t cow_faults;
    
    // Configuration
    bool enable_cow;                    // Copy-on-write
    bool enable_swap;                   // Swapping support
    bool enable_demand_paging;          // Demand paging
} vmm_t;

// ===============================================================================
// CORE VMM FUNCTIONS
// ===============================================================================

// Initialization
int vmm_init(void);
void vmm_shutdown(void);

// Address space management
address_space_t *vmm_create_address_space(void);
void vmm_destroy_address_space(address_space_t *space);
address_space_t *vmm_clone_address_space(address_space_t *space);

// Address space switching
void vmm_switch_address_space(address_space_t *space);
address_space_t *vmm_get_current_space(void);

// ===============================================================================
// VMA MANAGEMENT
// ===============================================================================

// VMA creation and destruction
vma_t *vmm_create_vma(uintptr_t start, size_t size, vma_type_t type, uint32_t flags);
void vmm_destroy_vma(vma_t *vma);

// VMA operations
int vmm_add_vma(address_space_t *space, vma_t *vma);
int vmm_remove_vma(address_space_t *space, vma_t *vma);
vma_t *vmm_find_vma(address_space_t *space, uintptr_t addr);
vma_t *vmm_find_vma_intersection(address_space_t *space, uintptr_t start, uintptr_t end);

// VMA modification
int vmm_expand_vma(vma_t *vma, size_t new_size);
int vmm_shrink_vma(vma_t *vma, size_t new_size);
int vmm_split_vma(vma_t *vma, uintptr_t split_addr);
int vmm_merge_vmas(vma_t *vma1, vma_t *vma2);

// ===============================================================================
// PAGE MAPPING FUNCTIONS
// ===============================================================================

// High-level mapping
int vmm_map_pages(address_space_t *space, uintptr_t virt_addr, uintptr_t phys_addr, 
                  size_t size, uint32_t flags);
int vmm_unmap_pages(address_space_t *space, uintptr_t virt_addr, size_t size);

// Single page operations
int vmm_map_page(address_space_t *space, uintptr_t virt_addr, uintptr_t phys_addr, uint32_t flags);
int vmm_unmap_page(address_space_t *space, uintptr_t virt_addr);

// Page table management
uint64_t *vmm_get_or_create_pml4(address_space_t *space);
uint64_t *vmm_get_or_create_pdpt(uint64_t *pml4, uintptr_t virt_addr);
uint64_t *vmm_get_or_create_pd(uint64_t *pdpt, uintptr_t virt_addr);
uint64_t *vmm_get_or_create_pt(uint64_t *pd, uintptr_t virt_addr);

// ===============================================================================
// MEMORY ALLOCATION FUNCTIONS
// ===============================================================================

// Virtual memory allocation
uintptr_t vmm_alloc_virtual_pages(address_space_t *space, size_t count, uint32_t flags);
void vmm_free_virtual_pages(address_space_t *space, uintptr_t virt_addr, size_t count);

// Heap management
uintptr_t vmm_expand_heap(address_space_t *space, size_t size);
int vmm_shrink_heap(address_space_t *space, size_t size);

// Stack management
uintptr_t vmm_expand_stack(address_space_t *space, size_t size);

// mmap implementation
uintptr_t vmm_mmap(address_space_t *space, uintptr_t addr, size_t length, 
                   int prot, int flags, int fd, int64_t offset);
int vmm_munmap(address_space_t *space, uintptr_t addr, size_t length);

// ===============================================================================
// PAGE FAULT HANDLING
// ===============================================================================

// Page fault handler
int vmm_handle_page_fault(uintptr_t fault_addr, uint32_t error_code);

// Fault types
#define FAULT_TYPE_PROTECTION   0x01
#define FAULT_TYPE_NOT_PRESENT  0x02
#define FAULT_TYPE_WRITE        0x04
#define FAULT_TYPE_USER         0x08

// Demand paging
int vmm_handle_demand_page(address_space_t *space, uintptr_t fault_addr);
int vmm_handle_cow_fault(address_space_t *space, uintptr_t fault_addr);

// ===============================================================================
// ADDRESS TRANSLATION
// ===============================================================================

// Virtual to physical translation
uintptr_t vmm_virt_to_phys(address_space_t *space, uintptr_t virt_addr);
bool vmm_is_mapped(address_space_t *space, uintptr_t virt_addr);

// Page table walking
uint64_t vmm_walk_page_table(uint64_t *pml4, uintptr_t virt_addr);

// ===============================================================================
// UTILITY FUNCTIONS
// ===============================================================================

// Address validation
bool vmm_is_valid_user_addr(uintptr_t addr);
bool vmm_is_valid_kernel_addr(uintptr_t addr);
bool vmm_is_canonical_addr(uintptr_t addr);

// Page table utilities
void vmm_flush_tlb(void);
void vmm_flush_tlb_page(uintptr_t virt_addr);
void vmm_invalidate_page(uintptr_t virt_addr);

// Statistics and debugging
void vmm_print_stats(void);
void vmm_print_address_space(address_space_t *space);
void vmm_print_vma_list(address_space_t *space);
void vmm_dump_page_tables(address_space_t *space);

// ===============================================================================
// GLOBAL VARIABLES
// ===============================================================================

extern vmm_t *g_vmm;