// Simple unified memory allocator implementation
#include "allocator.h"
#include <ir0/print.h>
#include <string.h>

static simple_allocator_t g_allocator;
static int g_initialized = 0;

// Dummy for scheduler detection (not used)
uint32_t free_pages_count = 1000;

void simple_alloc_init(void)
{
    if (g_initialized)
        return;

    g_allocator.start = (void *)SIMPLE_HEAP_START;
    g_allocator.end = (void *)SIMPLE_HEAP_END;
    g_allocator.current = g_allocator.start;
    g_allocator.total_size = SIMPLE_HEAP_SIZE;
    g_allocator.used = 0;
    g_allocator.allocations = 0;

    g_initialized = 1;
}

void *simple_alloc(size_t size)
{
    if (!g_initialized)
    {
        simple_alloc_init();
    }

    if (size == 0)
        return NULL;

    // Align to 16 bytes
    size = (size + 15) & ~15;

    // Check if we have space
    if ((uintptr_t)g_allocator.current + size > (uintptr_t)g_allocator.end)
    {
        return NULL; // Out of memory
    }

    void *ptr = g_allocator.current;
    g_allocator.current = (void *)((uintptr_t)g_allocator.current + size);
    g_allocator.used += size;
    g_allocator.allocations++;

    // Zero the memory
    memset(ptr, 0, size);

    return ptr;
}

void simple_free(void *ptr)
{
    // Bump allocator - no free
    (void)ptr;
}

void simple_alloc_stats(size_t *total, size_t *used, size_t *allocs)
{
    if (total)
        *total = g_allocator.total_size;
    if (used)
        *used = g_allocator.used;
    if (allocs)
        *allocs = g_allocator.allocations;
}

void simple_alloc_trace(void)
{
    print("=== Memory Allocator ===\n");
    print("Start: 0x");
    print_hex64((uintptr_t)g_allocator.start);
    print("\nCurrent: 0x");
    print_hex64((uintptr_t)g_allocator.current);
    print("\nEnd: 0x");
    print_hex64((uintptr_t)g_allocator.end);
    print("\nUsed: ");
    print_uint32(g_allocator.used);
    print(" / ");
    print_uint32(g_allocator.total_size);
    print(" bytes\n");
    print("Allocations: ");
    print_uint32(g_allocator.allocations);
    print("\n");
}

// Compatibility wrappers for existing code
void *kmalloc(size_t size)
{
    return simple_alloc(size);
}

void kfree(void *ptr)
{
    simple_free(ptr);
}

void *krealloc(void *ptr, size_t new_size)
{
    if (!ptr)
        return simple_alloc(new_size);
    // Simple implementation: allocate new and copy
    // We don't know old size, so just allocate new
    return simple_alloc(new_size);
}

void heap_init(void)
{
    simple_alloc_init();
}
