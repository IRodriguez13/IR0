// ===============================================================================
// IR0 KERNEL - PHYSICAL PAGE ALLOCATOR
// ===============================================================================

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ===============================================================================
// PHYSICAL MEMORY CONSTANTS
// ===============================================================================

#define PAGE_SIZE_4KB           0x1000      // 4KB
#define PAGE_SIZE_2MB           0x200000    // 2MB  
#define PAGE_SIZE_1GB           0x40000000  // 1GB

#define MAX_PHYSICAL_PAGES      0x100000    // 1M pages = 4GB max
#define BITMAP_SIZE             (MAX_PHYSICAL_PAGES / 8)

// Memory zones
#define ZONE_DMA_START          0x0         // 0-16MB (DMA)
#define ZONE_DMA_END            0x1000000   
#define ZONE_NORMAL_START       0x1000000   // 16MB-896MB (Normal)
#define ZONE_NORMAL_END         0x38000000  
#define ZONE_HIGHMEM_START      0x38000000  // >896MB (High memory)

// ===============================================================================
// PHYSICAL PAGE FRAME STRUCTURE
// ===============================================================================

typedef struct page_frame {
    uint32_t flags;           // Page flags (free, reserved, etc.)
    uint32_t ref_count;       // Reference count
    uint32_t zone;            // Memory zone
    struct page_frame *next;  // For free lists
} page_frame_t;

// Page flags
#define PAGE_FLAG_FREE          0x01
#define PAGE_FLAG_RESERVED      0x02
#define PAGE_FLAG_KERNEL        0x04
#define PAGE_FLAG_USER          0x08
#define PAGE_FLAG_DMA           0x10

// ===============================================================================
// PHYSICAL ALLOCATOR STRUCTURE
// ===============================================================================

typedef struct physical_allocator {
    // Memory layout
    uintptr_t memory_start;
    uintptr_t memory_end;
    size_t total_pages;
    size_t free_pages;
    
    // Bitmap for page tracking
    uint8_t *page_bitmap;
    
    // Page frame array
    page_frame_t *page_frames;
    
    // Free lists by zone
    page_frame_t *free_lists[3];  // DMA, Normal, HighMem
    
    // Statistics
    size_t allocated_pages;
    size_t reserved_pages;
    size_t dma_pages;
    size_t normal_pages;
    size_t highmem_pages;
} physical_allocator_t;

// ===============================================================================
// CORE FUNCTIONS
// ===============================================================================

// Initialization
int physical_allocator_init(uintptr_t memory_start, uintptr_t memory_end);
void physical_allocator_shutdown(void);

// Page allocation
uintptr_t alloc_physical_page(void);
uintptr_t alloc_physical_pages(size_t count);
uintptr_t alloc_physical_page_in_zone(int zone);

// Page deallocation  
void free_physical_page(uintptr_t page_addr);
void free_physical_pages(uintptr_t page_addr, size_t count);

// Page information
bool is_physical_page_free(uintptr_t page_addr);
int get_physical_page_zone(uintptr_t page_addr);
size_t get_physical_page_ref_count(uintptr_t page_addr);

// Reference counting
void inc_physical_page_ref(uintptr_t page_addr);
void dec_physical_page_ref(uintptr_t page_addr);

// ===============================================================================
// UTILITY FUNCTIONS
// ===============================================================================

// Address conversion
uintptr_t page_to_pfn(uintptr_t page_addr);
uintptr_t pfn_to_page(uintptr_t pfn);
page_frame_t *pfn_to_page_frame(uintptr_t pfn);

// Statistics
void physical_allocator_print_stats(void);
size_t get_free_physical_pages(void);
size_t get_total_physical_pages(void);

// Memory detection
void detect_physical_memory(void);
void reserve_kernel_memory(uintptr_t start, uintptr_t end);

// ===============================================================================
// GLOBAL ALLOCATOR
// ===============================================================================

extern physical_allocator_t *g_physical_allocator;
