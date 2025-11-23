// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: kmem.c
 * Description: Kernel heap allocator with automatic debug tracking
 */
#include "kmem.h"
#include <ir0/memory/allocator.h>
#include <ir0/oops.h>

/* Compiler optimization hints */
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/**
 * __kmalloc_impl - Internal implementation of kernel memory allocation
 * Pure implementation without validation - called by checked wrapper
 */
void *__kmalloc_impl(size_t size)
{
	if (unlikely(size == 0))
		return NULL;

	return alloc(size);
}

/**
 * __kfree_impl - Internal implementation of kernel memory free
 * Pure implementation without validation - called by checked wrapper
 */
void __kfree_impl(void *ptr)
{
	if (likely(ptr))
		alloc_free(ptr);
}

/**
 * __krealloc_impl - Internal implementation of kernel memory reallocation
 * Pure implementation without validation - called by checked wrapper
 */
void *__krealloc_impl(void *ptr, size_t size)
{
	if (unlikely(size == 0))
	{
		__kfree_impl(ptr);
		return NULL;
	}

	return all_realloc(ptr, size);
}

/**
 * __kmalloc_aligned_impl - Internal implementation of aligned allocation
 * Pure implementation without validation - called by checked wrapper
 */
void *__kmalloc_aligned_impl(size_t size, size_t alignment)
{
	if (size == 0 || alignment == 0)
		return NULL;

	// Ensure alignment is power of 2
	if ((alignment & (alignment - 1)) != 0)
		return NULL;

	// Allocate extra space to ensure we can align
	size_t total_size = size + alignment - 1 + sizeof(void *);
	void *raw_ptr = __kmalloc_impl(total_size);
	if (!raw_ptr)
		return NULL;

	// Calculate aligned address
	uintptr_t raw_addr = (uintptr_t)raw_ptr;
	uintptr_t aligned_addr = (raw_addr + sizeof(void *) + alignment - 1) & ~(alignment - 1);
	void *aligned_ptr = (void *)aligned_addr;

	// Store original pointer just before aligned address
	void **orig_ptr_storage = (void **)aligned_ptr - 1;
	*orig_ptr_storage = raw_ptr;

	return aligned_ptr;
}

/**
 * __kfree_aligned_impl - Internal implementation of aligned free
 * Pure implementation without validation - called by checked wrapper
 */
void __kfree_aligned_impl(void *ptr)
{
	if (!ptr)
		return;

	// Get original pointer stored before aligned address
	void **orig_ptr_storage = (void **)ptr - 1;
	void *orig_ptr = *orig_ptr_storage;

	// Free the original allocation
	__kfree_impl(orig_ptr);
}


/**
 * __kmalloc_checked - Checked wrapper for kmalloc with automatic debug tracking
 * Validates parameters and calls implementation
 */
void *__kmalloc_checked(size_t size, const char *file, int line, const char *caller)
{
	if (unlikely(size == 0)) {
		panicex("kmalloc: size is 0", PANIC_MEM, file, line, caller);
	}

	if (unlikely(size > SIMPLE_HEAP_SIZE)) {
		panicex("kmalloc: size too large (possible overflow)", PANIC_MEM, file, line, caller);
	}

	void *ptr = __kmalloc_impl(size);

	if (unlikely(!ptr)) {
		panicex("kmalloc: out of memory", PANIC_OUT_OF_MEMORY, file, line, caller);
	}

	return ptr;
}

/**
 * __kfree_checked - Checked wrapper for kfree with automatic debug tracking
 * Validates parameters and calls implementation
 */
void __kfree_checked(void *ptr, const char *file, int line, const char *caller)
{
	if (unlikely(!ptr)) {
		// Linux allows free(NULL), but we can warn about it
		// For now, we'll allow it silently like the original implementation
		return;
	}

	// Validate that the pointer is within heap range
	if (unlikely((uintptr_t)ptr < SIMPLE_HEAP_START || 
	             (uintptr_t)ptr >= SIMPLE_HEAP_END)) {
		panicex("kfree: pointer out of heap range", PANIC_MEM, file, line, caller);
	}

	__kfree_impl(ptr);
}

/**
 * __krealloc_checked - Checked wrapper for krealloc with automatic debug tracking
 * Validates parameters and calls implementation
 */
void *__krealloc_checked(void *ptr, size_t new_size, 
                        const char *file, int line, const char *caller)
{
	if (unlikely(new_size == 0)) {
		// realloc(ptr, 0) is equivalent to free(ptr)
		__kfree_checked(ptr, file, line, caller);
		return NULL;
	}

	if (unlikely(new_size > SIMPLE_HEAP_SIZE)) {
		panicex("krealloc: new_size too large", PANIC_MEM, file, line, caller);
	}

	// Validate existing pointer if not NULL
	if (ptr && unlikely((uintptr_t)ptr < SIMPLE_HEAP_START || 
	                    (uintptr_t)ptr >= SIMPLE_HEAP_END)) {
		panicex("krealloc: invalid pointer", PANIC_MEM, file, line, caller);
	}

	void *new_ptr = __krealloc_impl(ptr, new_size);

	if (unlikely(!new_ptr)) {
		panicex("krealloc: out of memory", PANIC_OUT_OF_MEMORY, file, line, caller);
	}

	return new_ptr;
}

/**
 * __kmalloc_aligned_checked - Checked wrapper for aligned allocation
 * Validates parameters and calls implementation
 */
void *__kmalloc_aligned_checked(size_t size, size_t alignment,
                               const char *file, int line, const char *caller)
{
	if (unlikely(size == 0)) {
		panicex("kmalloc_aligned: size is 0", PANIC_MEM, file, line, caller);
	}

	if (unlikely(alignment == 0)) {
		panicex("kmalloc_aligned: alignment is 0", PANIC_MEM, file, line, caller);
	}

	// Ensure alignment is power of 2
	if (unlikely((alignment & (alignment - 1)) != 0)) {
		panicex("kmalloc_aligned: alignment not power of 2", PANIC_MEM, file, line, caller);
	}

	if (unlikely(size > SIMPLE_HEAP_SIZE)) {
		panicex("kmalloc_aligned: size too large", PANIC_MEM, file, line, caller);
	}

	void *ptr = __kmalloc_aligned_impl(size, alignment);

	if (unlikely(!ptr)) {
		panicex("kmalloc_aligned: out of memory", PANIC_OUT_OF_MEMORY, file, line, caller);
	}

	return ptr;
}

/**
 * __kfree_aligned_checked - Checked wrapper for aligned free
 * Validates parameters and calls implementation
 */
void __kfree_aligned_checked(void *ptr, const char *file, int line, const char *caller)
{
	if (unlikely(!ptr)) {
		// Allow NULL like regular kfree
		return;
	}

	// Validate that the pointer is within heap range
	if (unlikely((uintptr_t)ptr < SIMPLE_HEAP_START || 
	             (uintptr_t)ptr >= SIMPLE_HEAP_END)) {
		panicex("kfree_aligned: pointer out of heap range", PANIC_MEM, file, line, caller);
	}

	__kfree_aligned_impl(ptr);
}

/**
 * heap_init - initialize kernel heap allocator
 */
void heap_init(void)
{
	alloc_init();
}
