/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: kmem.h
 * Description: Kernel heap allocation API (kmalloc/kfree)
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void heap_init(void);

/*
 * kmalloc - allocate @size bytes of zero-filled kernel heap memory.
 * Returns NULL when @size is 0, exceeds the heap, or the heap is exhausted;
 * callers handle NULL. Release with kfree().
 */
void *kmalloc(size_t size);

/*
 * kcalloc - kmalloc() for an array of @n elements of @size bytes.
 * Returns NULL when n * size overflows size_t. Release with kfree().
 */
void *kcalloc(size_t n, size_t size);

/*
 * kmalloc_aligned - kmalloc() whose result is aligned to @alignment, a
 * power of two. Returns NULL on a zero or non-power-of-two alignment.
 * Release with kfree_aligned(), never kfree().
 */
void *kmalloc_aligned(size_t size, size_t alignment);

void __kfree_checked(void *ptr, const char *file, int line, const char *caller);
void __kfree_aligned_checked(void *ptr, const char *file, int line, const char *caller);

#ifdef __cplusplus
}
#endif

/*
 * kfree / kfree_aligned - release memory from kmalloc() / kmalloc_aligned().
 * NULL is a no-op; a pointer outside the heap panics naming the call site.
 */
#define kfree(ptr) \
	__kfree_checked((ptr), __FILE__, __LINE__, __func__)

#define kfree_aligned(ptr) \
	__kfree_aligned_checked((ptr), __FILE__, __LINE__, __func__)
