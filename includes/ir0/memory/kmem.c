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
 * Description: Kernel heap allocator public interface
 */
#include "kmem.h"
#include <ir0/memory/allocator.h>

/* Compiler optimization hints */
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/**
 * kmalloc - allocate kernel memory
 * @size: number of bytes to allocate
 *
 * Returns pointer to allocated memory or NULL on failure.
 */
void *kmalloc(size_t size)
{
	if (unlikely(size == 0))
		return NULL;

	return alloc(size);
}

/**
 * kfree - free kernel memory
 * @ptr: pointer to memory to free
 */
void kfree(void *ptr)
{
	if (likely(ptr))
		alloc_free(ptr);
}

/**
 * krealloc - reallocate kernel memory
 * @ptr: pointer to existing memory
 * @size: new size in bytes
 *
 * Returns pointer to reallocated memory or NULL on failure.
 */
void *krealloc(void *ptr, size_t size)
{
	if (unlikely(size == 0))
	{
		kfree(ptr);
		return NULL;
	}

	return all_realloc(ptr, size);
}

/**
 * heap_init - initialize kernel heap allocator
 */
void heap_init(void)
{
	alloc_init();
}
