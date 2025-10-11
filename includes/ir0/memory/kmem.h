#pragma once
#include <stdint.h>
#include <stddef.h>

// void* kalloc(size_t size);
void kfree(void* ptr);
void *krealloc(void *ptr, size_t new_size);
void* kmalloc(size_t size);
void heap_init(void);


