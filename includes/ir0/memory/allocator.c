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
#include <ir0/memory/kmem.h>
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

void *alloc(size_t size)
{
    if (!allocator.initialized)
        alloc_init();

    if (size == 0)
        return NULL;

    /* Align to 16 bytes and add header + footer size */
    size_t total_size = (size + sizeof(block_header_t) + sizeof(block_footer_t) + 15) & ~15;

    /* Find a free block that fits (first-fit) */
    block_header_t *current = allocator.free_list;

    while (current)
    {
        if (current->is_free && current->size >= total_size)
        {
            /* Found a suitable block */
            current->is_free = 0;

            /* Split block if it's much larger than needed */
            if (current->size > total_size + sizeof(block_header_t) + sizeof(block_footer_t) + 32)
            {
                /* Create new block in the remaining space */
                block_header_t *new_block = (block_header_t *)((char *)current + total_size);
                size_t new_size = current->size - total_size;
                
                set_block(new_block, new_size, 1);
                
                /* Insert into free list */
                new_block->next = current->next;
                new_block->prev = current->prev;
                
                if (current->next)
                    current->next->prev = new_block;
                if (current->prev)
                    current->prev->next = new_block;
                else
                    allocator.free_list = new_block; /* New block is now head */

                /* Update current block size */
                set_block(current, total_size, 0);
                
                /* Remove current from free list since it's allocated */
                current->next = NULL;
                current->prev = NULL;
            }
            else
            {
                /* Use entire block - remove from free list */
                if (current->prev)
                    current->prev->next = current->next;
                else
                    allocator.free_list = current->next;
                
                if (current->next)
                    current->next->prev = current->prev;
                
                /* Update footer */
                block_footer_t *footer = get_footer(current);
                footer->is_free = 0;
            }

            allocator.total_allocated += current->size;

            /* Return pointer after header */
            void *ptr = (char *)current + sizeof(block_header_t);

            /* Zero the memory */
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