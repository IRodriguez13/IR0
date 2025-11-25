// malloc.c - Linked-list heap allocator for IR0 userland
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h> // For memset/memcpy

// Block metadata
typedef struct block_meta {
    size_t size;
    struct block_meta *next;
    int free;
    int magic; // To verify block integrity
} block_meta;

#define META_SIZE sizeof(block_meta)
#define MAGIC 0x12345678

static void *global_base = NULL;

// Find a free block that fits the requested size
static block_meta *find_free_block(block_meta **last, size_t size) {
    block_meta *current = global_base;
    while (current && !(current->free && current->size >= size)) {
        *last = current;
        current = current->next;
    }
    return current;
}

// Request more space from the OS
static block_meta *request_space(block_meta *last, size_t size) {
    block_meta *block;
    block = sbrk(0);
    void *request = sbrk(size + META_SIZE);
    
    if (request == (void*) -1) {
        return NULL; // sbrk failed
    }

    if (last) {
        last->next = block;
    }

    block->size = size;
    block->next = NULL;
    block->free = 0;
    block->magic = MAGIC;
    return block;
}

void *malloc(size_t size) {
    block_meta *block;
    size_t aligned_size = (size + 7) & ~7; // Align to 8 bytes

    if (size <= 0) {
        return NULL;
    }

    if (!global_base) {
        // First call
        block = request_space(NULL, aligned_size);
        if (!block) {
            return NULL;
        }
        global_base = block;
    } else {
        block_meta *last = global_base;
        block = find_free_block(&last, aligned_size);
        if (!block) {
            // No free block found, request new one
            block = request_space(last, aligned_size);
            if (!block) {
                return NULL;
            }
        } else {
            // Found a free block
            // TODO: Split block if it's too large?
            block->free = 0;
            block->magic = MAGIC;
        }
    }

    return (block + 1);
}

void free(void *ptr) {
    if (!ptr) {
        return;
    }

    block_meta *block_ptr = (block_meta*)ptr - 1;
    if (block_ptr->magic != MAGIC) {
        // Invalid pointer or corruption
        return;
    }

    block_ptr->free = 1;

    // Coalesce? 
    // For simplicity in this step, we won't coalesce immediately, 
    // but we can iterate to merge.
    // Let's do a simple merge pass if we can easily access next.
    // Since we have a singly linked list, merging with PREVIOUS is hard without traversal.
    // Merging with NEXT is easy.
    
    if (block_ptr->next && block_ptr->next->free) 
    {
        block_ptr->size += block_ptr->next->size + META_SIZE;
        block_ptr->next = block_ptr->next->next;
    }
    
    // Ideally we should also check if we can merge with previous, but that requires
    // traversing from start or a doubly linked list.
    // We'll stick to this for now.
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) {
        return malloc(size);
    }

    block_meta *block_ptr = (block_meta*)ptr - 1;
    if (block_ptr->size >= size) {
        // We have enough space
        return ptr;
    }

    // Need new block
    void *new_ptr = malloc(size);
    if (!new_ptr) {
        return NULL;
    }

    memcpy(new_ptr, ptr, block_ptr->size);
    free(ptr);
    return new_ptr;
}
