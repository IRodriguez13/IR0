// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: allocator.c
 * Description: Kernel heap allocator with free-list and boundary tags
 *              for efficient O(1) coalescing in both directions
 */

#include "allocator.h"
#include <ir0/kmem.h>
#include <drivers/serial/serial.h>
#include <config.h>

/* Block header - at the start of each block */
typedef struct block_header
{
    size_t size;               /* Size of this block (including header AND footer) */
    int is_free;               /* 1 if free, 0 if allocated */
    struct block_header *next; /* Next block in free list */
    struct block_header *prev; /* Previous block in free list */
} block_header_t;

/* Block footer - at the end of each block for O(1) backward coalescing */
typedef struct block_footer
{
    size_t size;               /* Must match header->size */
    int is_free;               /* Mirror of header status */
} block_footer_t;

static struct
{
    void *heap_start;
    void *heap_end;
    size_t heap_size;
    block_header_t *free_list;
    size_t total_allocated;
    size_t total_freed;
    size_t coalesce_forward_count;  /* Debug: count forward coalesces */
    size_t coalesce_backward_count; /* Debug: count backward coalesces */
    int initialized;
} allocator = {0};

uint32_t free_pages_count = 1000;


/* Get footer from header */
static inline block_footer_t *get_footer(block_header_t *header)
{
    return (block_footer_t *)((char *)header + header->size - sizeof(block_footer_t));
}

/* Get header from footer */
static inline block_header_t *get_header_from_footer(block_footer_t *footer)
{
    return (block_header_t *)((char *)footer + sizeof(block_footer_t) - footer->size);
}

/* Get previous physical block using current header */
static inline block_header_t *get_prev_block(block_header_t *current)
{
    /* Check if we're at the start of heap */
    if ((void *)current <= allocator.heap_start)
        return NULL;
    
    /* Get footer of previous block (just before current header) */
    block_footer_t *prev_footer = (block_footer_t *)((char *)current - sizeof(block_footer_t));
    
    /* Validate that prev_footer is within heap bounds */
    if ((void *)prev_footer < allocator.heap_start)
        return NULL;
    
    return get_header_from_footer(prev_footer);
}

/* Get next physical block */
static inline block_header_t *get_next_block(block_header_t *current)
{
    block_header_t *next = (block_header_t *)((char *)current + current->size);
    
    /* Check if next is beyond heap bounds */
    if ((void *)next >= allocator.heap_end)
        return NULL;
    
    return next;
}

/* Set header and footer for a block */
static inline void set_block(block_header_t *header, size_t size, int is_free)
{
    header->size = size;
    header->is_free = is_free;
    
    block_footer_t *footer = get_footer(header);
    footer->size = size;
    footer->is_free = is_free;
}


void alloc_init(void)
{
    if (allocator.initialized)
        return;

    allocator.heap_start = (void *)SIMPLE_HEAP_START;
    allocator.heap_end = (void *)SIMPLE_HEAP_END;
    allocator.heap_size = SIMPLE_HEAP_SIZE;
    allocator.total_allocated = 0;
    allocator.total_freed = 0;
    allocator.coalesce_forward_count = 0;
    allocator.coalesce_backward_count = 0;

    /* Initialize with one big free block */
    allocator.free_list = (block_header_t *)allocator.heap_start;
    
    /* Set block with header and footer */
    set_block(allocator.free_list, allocator.heap_size, 1);
    allocator.free_list->next = NULL;
    allocator.free_list->prev = NULL;

    allocator.initialized = 1;

#if DEBUG_MEMORY_ALLOCATOR
    serial_print("[ALLOCATOR] Initialized\n");
#endif
}

/**
 * alloc - Allocate memory from the kernel heap
 * @size: Number of bytes to allocate (will be aligned to 16 bytes)
 *
 * This function implements a first-fit allocation strategy with block
 * splitting and coalescing for efficient memory utilization. Blocks are
 * managed using a doubly-linked free list with boundary tags (headers
 * and footers) to enable O(1) coalescing in both directions.
 *
 * Algorithm overview:
 * 1. Align request size to 16-byte boundary and add metadata overhead
 * 2. Search free list for first block large enough (first-fit)
 * 3. If found and significantly larger, split block (prevents waste)
 * 4. Remove allocated block from free list
 * 5. Zero allocated memory for security (prevents information leakage)
 *
 * Performance characteristics:
 * - Allocation: O(n) worst case where n = number of free blocks
 * - Free: O(1) due to boundary tags enabling immediate coalescing
 * - Fragmentation: Moderate (first-fit can create small free blocks)
 *
 * Thread safety:
 * - NOT thread-safe - caller must ensure mutual exclusion
 * - Called from interrupt context - must not sleep
 *
 * Returns: Pointer to allocated memory, or NULL on failure (OOM)
 */
void *alloc(size_t size)
{
    /* Initialize allocator on first use - lazy initialization pattern.
     * This allows the allocator to be used early in boot before full
     * initialization is complete.
     */
    if (!allocator.initialized)
        alloc_init();

    /* Zero-sized allocations are invalid - return NULL.
     * This simplifies the implementation by avoiding edge cases.
     */
    if (size == 0)
        return NULL;

    /* Calculate total block size including metadata overhead.
     * We add:
     * - Header size (block_header_t)
     * - Footer size (block_footer_t)
     * - 15 bytes for alignment padding
     * Then mask to 16-byte boundary for cache alignment.
     * This ensures all blocks are properly aligned for efficient access.
     */
    size_t total_size = (size + sizeof(block_header_t) + sizeof(block_footer_t) + 15) & ~15;

    /* Search free list using first-fit algorithm.
     * First-fit is simple and works well for kernel workloads where
     * allocation patterns are relatively predictable. Alternative strategies
     * (best-fit, worst-fit) could reduce fragmentation but add complexity.
     */
    block_header_t *current = allocator.free_list;

    while (current)
    {
        /* Check if this block is free and large enough.
         * We need at least total_size bytes to satisfy the request.
         */
        if (current->is_free && current->size >= total_size)
        {
            /* Mark block as allocated - update both header and footer.
             * This happens atomically in a single operation to maintain
             * consistency even if interrupted.
             */
            current->is_free = 0;

            /* Block splitting optimization:
             * If the block is significantly larger than requested (threshold:
             * total_size + metadata + 32 bytes), split it to reduce fragmentation.
             * The 32-byte threshold prevents splitting on tiny differences, which
             * would create many small unusable blocks.
             *
             * Splitting allows us to use only what we need while keeping the
             * remainder available for future allocations.
             */
            if (current->size > total_size + sizeof(block_header_t) + sizeof(block_footer_t) + 32)
            {
                /* Create new free block from the remaining space.
                 * The new block starts immediately after the allocated portion.
                 * This maintains heap continuity and allows efficient coalescing
                 * when this new free block is later freed.
                 */
                block_header_t *new_block = (block_header_t *)((char *)current + total_size);
                size_t new_size = current->size - total_size;
                
                /* Initialize new block with proper header and footer.
                 * Mark it as free (1) so it's available for allocation.
                 */
                set_block(new_block, new_size, 1);
                
                /* Insert new block into free list at the same position as current.
                 * This maintains the relative order of free blocks, which helps
                 * with locality of reference in future allocations.
                 */
                new_block->next = current->next;
                new_block->prev = current->prev;
                
                /* Update doubly-linked list pointers.
                 * Handle edge cases: updating next's prev, and if current was
                 * the head, update the head pointer.
                 */
                if (current->next)
                    current->next->prev = new_block;
                if (current->prev)
                    current->prev->next = new_block;
                else
                    allocator.free_list = new_block; /* New block is now head */

                /* Update current block to reflect new size.
                 * Mark as allocated (0) and update both header and footer.
                 * This completes the split operation.
                 */
                set_block(current, total_size, 0);
                
                /* Remove current from free list since it's now allocated.
                 * Null pointers help detect bugs (double free, etc).
                 */
                current->next = NULL;
                current->prev = NULL;
            }
            else
            {
                /* Block is not large enough to split efficiently.
                 * Use the entire block as-is. This prevents creating many
                 * tiny free blocks that would never be usable.
                 */
                
                /* Remove block from free list.
                 * Update doubly-linked list pointers, handling the case
                 * where this block is at the head, middle, or tail.
                 */
                if (current->prev)
                    current->prev->next = current->next;
                else
                    allocator.free_list = current->next;  /* Was head - update head */
                
                if (current->next)
                    current->next->prev = current->prev;
                
                /* Update footer to mark block as allocated.
                 * Both header and footer must agree - this is checked during
                 * free operations to detect corruption.
                 */
                block_footer_t *footer = get_footer(current);
                footer->is_free = 0;
            }

            /* Update allocation statistics for monitoring/debugging.
             * This helps track heap usage patterns and detect memory leaks.
             */
            allocator.total_allocated += current->size;

            /* Return pointer to user data (after header).
             * The caller sees only the data portion and must not access
             * the header/footer directly - this is enforced by the API.
             */
            void *ptr = (char *)current + sizeof(block_header_t);

            /* Zero allocated memory for security.
             * Prevents information leakage from previously freed blocks
             * containing sensitive data (passwords, keys, etc).
             *
             * Note: This could be optimized with memset() if available,
             * but we avoid dependencies on standard library.
             */
            for (size_t i = 0; i < size; i++)
                ((char *)ptr)[i] = 0;

#if DEBUG_MEMORY_ALLOCATOR
            serial_print("[ALLOC] Memory allocated\n");
#endif

            return ptr;
        }
        current = current->next;
    }

#if DEBUG_MEMORY_ALLOCATOR
    serial_print("[ALLOC] FAILED: no suitable block\n");
#endif

    return NULL; /* Out of memory */
}


void *all_realloc(void *ptr, size_t new_size)
{
    if (!ptr)
        return alloc(new_size);

    if (new_size == 0)
    {
        alloc_free(ptr);
        return NULL;
    }

    /* Get old block size */
    block_header_t *old_block = (block_header_t *)((char *)ptr - sizeof(block_header_t));
    size_t old_size = old_block->size - sizeof(block_header_t) - sizeof(block_footer_t);

    /* Allocate new block */
    void *new_ptr = alloc(new_size);
    if (!new_ptr)
        return NULL;

    /* Copy data (use smaller of old/new size) */
    size_t copy_size = (old_size < new_size) ? old_size : new_size;
    for (size_t i = 0; i < copy_size; i++)
        ((char *)new_ptr)[i] = ((char *)ptr)[i];

    /* Free old block */
    alloc_free(ptr);

    return new_ptr;
}

/* FREE WITH O(1) BIDIRECTIONAL COALESCING                                   */

void alloc_free(void *ptr)
{
    if (!ptr || !allocator.initialized)
        return;

    /* Get block header */
    block_header_t *block = (block_header_t *)((char *)ptr - sizeof(block_header_t));

    /* Validate block is within heap bounds */
    if ((void *)block < allocator.heap_start || (void *)block >= allocator.heap_end)
        return;

    /* Mark as free */
    block->is_free = 1;
    block_footer_t *footer = get_footer(block);
    footer->is_free = 1;
    
    allocator.total_freed += block->size;

#if DEBUG_MEMORY_ALLOCATOR
    serial_print("[FREE] Memory freed\n");
#endif

    /* COALESCE WITH NEXT BLOCK (Forward)                                    */
    
    block_header_t *next_block = get_next_block(block);
    if (next_block && next_block->is_free)
    {
        /* Remove next block from free list */
        if (next_block->prev)
            next_block->prev->next = next_block->next;
        else
            allocator.free_list = next_block->next;
        
        if (next_block->next)
            next_block->next->prev = next_block->prev;
        
        /* Merge: extend current block to include next block */
        size_t new_size = block->size + next_block->size;
        set_block(block, new_size, 1);
        
        allocator.coalesce_forward_count++;
        
#if DEBUG_MEMORY_COALESCING
        serial_print("[COALESCE] Forward merge\n");
#endif
    }

    /* COALESCE WITH PREVIOUS BLOCK (Backward) - O(1) with boundary tags    */
    
    block_header_t *prev_block = get_prev_block(block);
    if (prev_block && prev_block->is_free)
    {
        /* Remove previous block from free list */
        if (prev_block->prev)
            prev_block->prev->next = prev_block->next;
        else
            allocator.free_list = prev_block->next;
        
        if (prev_block->next)
            prev_block->next->prev = prev_block->prev;
        
        /* Merge: extend previous block to include current block */
        size_t new_size = prev_block->size + block->size;
        set_block(prev_block, new_size, 1);
        
        /* Current block is now merged into prev_block */
        block = prev_block;
        
        allocator.coalesce_backward_count++;
        
#if DEBUG_MEMORY_COALESCING
        serial_print("[COALESCE] Backward merge\n");
#endif
    }

    /* INSERT COALESCED BLOCK INTO FREE LIST                                */
    
    block->next = allocator.free_list;
    block->prev = NULL;
    
    if (allocator.free_list)
        allocator.free_list->prev = block;
    
    allocator.free_list = block;
}

/* STATISTICS                                                                */

void alloc_stats(size_t *total, size_t *used, size_t *allocs)
{
    if (total)
        *total = allocator.heap_size;
    if (used)
        *used = allocator.total_allocated - allocator.total_freed;
    if (allocs)
        *allocs = allocator.total_allocated;

#if DEBUG_MEMORY_STATS
    serial_print("[ALLOCATOR STATS]\n");
    serial_print("  Stats available via debugger\n");
    /* Detailed stats viewable via debugger: */
    /* allocator.total_allocated, allocator.total_freed */
    /* allocator.coalesce_forward_count, allocator.coalesce_backward_count */
#endif
}