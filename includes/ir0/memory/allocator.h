// Simple unified memory allocator
#pragma once

#include <stdint.h>
#include <stddef.h>

// Memory layout (from boot: 0-32MB mapped)
// 0x000000 - 0x100000 : Reserved (BIOS, boot)
// 0x100000 - 0x800000 : Kernel code/data (1MB-8MB)
// 0x800000 - 0x2000000: Heap (8MB-32MB = 24MB heap)

#define SIMPLE_HEAP_START 0x800000
#define SIMPLE_HEAP_SIZE 0x1800000 // 24MB
#define SIMPLE_HEAP_END (SIMPLE_HEAP_START + SIMPLE_HEAP_SIZE)

typedef struct
{
    void *start;
    void *end;
    void *current;
    size_t total_size;
    size_t used;
    size_t allocations;
} simple_allocator_t;

// Initialize the allocator
void alloc_init(void);

// Allocate memory
void *alloc(size_t size);

// Free memory (no-op for now, we use bump allocation)
void alloc_free(void *ptr);

// Get stats
void alloc_stats(size_t *total, size_t *used, size_t *allocs);

// Reallocate memory
void *all_realloc(void *ptr, size_t new_size);

// Aligned allocation functions
void *kmalloc_aligned(size_t size, size_t alignment);

void kfree_aligned(void *ptr);

