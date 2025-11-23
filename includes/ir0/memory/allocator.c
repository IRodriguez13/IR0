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
 * Description: Kernel heap allocator with free-list implementation for dynamic memory management
 */

#include "allocator.h"
#include <ir0/memory/kmem.h>
#include <ir0/vga.h>

// Block header for free-list allocator
typedef struct block_header
{
    size_t size;               // Size of this block (including header)
    int is_free;               // 1 if free, 0 if allocated
    struct block_header *next; // Next block in free list
} block_header_t;

// Global allocator state
static struct
{
    void *heap_start;
    void *heap_end;
    size_t heap_size;
    block_header_t *free_list;
    size_t total_allocated;
    size_t total_freed;
    int initialized;
} allocator = {0};

uint32_t free_pages_count = 1000;

void alloc_init(void)
{
    if (allocator.initialized)
        return;

    allocator.heap_start = (void *)SIMPLE_HEAP_START;
    allocator.heap_end = (void *)SIMPLE_HEAP_END;
    allocator.heap_size = SIMPLE_HEAP_SIZE;
    allocator.total_allocated = 0;
    allocator.total_freed = 0;

    // Initialize with one big free block
    allocator.free_list = (block_header_t *)allocator.heap_start;
    allocator.free_list->size = allocator.heap_size;
    allocator.free_list->is_free = 1;
    allocator.free_list->next = NULL;

    allocator.initialized = 1;
}

void *alloc(size_t size)
{
    if (!allocator.initialized)
        alloc_init();

    if (size == 0)
        return NULL;

    // Align to 16 bytes and add header size
    size_t total_size = (size + sizeof(block_header_t) + 15) & ~15;

    // Find a free block that fits
    block_header_t *current = allocator.free_list;

    while (current)
    {
        if (current->is_free && current->size >= total_size)
        {
            // Found a suitable block
            current->is_free = 0;

            // Split block if it's much larger than needed
            if (current->size > total_size + sizeof(block_header_t) + 32)
            {
                block_header_t *new_block = (block_header_t *)((char *)current + total_size);
                new_block->size = current->size - total_size;
                new_block->is_free = 1;
                new_block->next = current->next;

                current->size = total_size;
                current->next = new_block;
            }

            allocator.total_allocated += current->size;

            // Return pointer after header
            void *ptr = (char *)current + sizeof(block_header_t);

            // Zero the memory
            for (size_t i = 0; i < size; i++)
                ((char *)ptr)[i] = 0;

            return ptr;
        }
        current = current->next;
    }

    return NULL; // Out of memory
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

    // Get old block size
    block_header_t *old_block = (block_header_t *)((char *)ptr - sizeof(block_header_t));
    size_t old_size = old_block->size - sizeof(block_header_t);

    // Allocate new block
    void *new_ptr = alloc(new_size);
    if (!new_ptr)
        return NULL;

    // Copy data (use smaller of old/new size)
    size_t copy_size = (old_size < new_size) ? old_size : new_size;
    for (size_t i = 0; i < copy_size; i++)
        ((char *)new_ptr)[i] = ((char *)ptr)[i];

    // Free old block
    alloc_free(ptr);

    return new_ptr;
}

void alloc_free(void *ptr)
{
    if (!ptr || !allocator.initialized)
        return;

    // Get block header
    block_header_t *block = (block_header_t *)((char *)ptr - sizeof(block_header_t));

    // Validate block is within heap bounds
    if ((void *)block < allocator.heap_start || (void *)block >= allocator.heap_end)
        return;

    // Mark as free
    block->is_free = 1;
    allocator.total_freed += block->size;

    // Coalesce with next block if it's free
    if (block->next && block->next->is_free)
    {
        block->size += block->next->size;
        block->next = block->next->next;
    }

    // Coalesce with previous block if it's free
    block_header_t *current = allocator.free_list;
    while (current && current->next != block)
    {
        current = current->next;
    }

    if (current && current->is_free && (char *)current + current->size == (char *)block)
    {
        current->size += block->size;
        current->next = block->next;
    }
}