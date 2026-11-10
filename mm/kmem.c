/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: kmem.c
 * Description: Kernel heap API (kmalloc/kfree) on top of the mm heap backend
 */

#include <ir0/kmem.h>
#include <mm/allocator.h>
#include <ir0/oops.h>
#include <ir0/mm_diag.h>
#include <ir0/overflow.h>

static int kmem_ptr_in_heap(const void *ptr)
{
	return (uintptr_t)ptr >= SIMPLE_HEAP_START &&
	       (uintptr_t)ptr < SIMPLE_HEAP_END;
}

void *kmalloc(size_t size)
{
	if (unlikely(size == 0 || size > SIMPLE_HEAP_SIZE))
		return NULL;
	if (mm_diag_fault_hit("kmem.alloc"))
		return NULL;
	return heap_alloc(size);
}

void *kcalloc(size_t n, size_t size)
{
	size_t bytes;

	if (ir0_mul_overflow(n, size, &bytes))
		return NULL;
	return kmalloc(bytes);
}

/*
 * The block is over-allocated by alignment - 1 plus one pointer; the raw
 * heap pointer is stored in the word just below the aligned address so
 * kfree_aligned() can hand it back to the backend.
 */
void *kmalloc_aligned(size_t size, size_t alignment)
{
	size_t total_size;
	size_t padding;
	uintptr_t aligned_addr;
	void *raw_ptr;

	if (unlikely(size == 0 || alignment == 0))
		return NULL;
	if (unlikely((alignment & (alignment - 1)) != 0))
		return NULL;
	if (unlikely(size > SIMPLE_HEAP_SIZE))
		return NULL;
	if (ir0_add_overflow(alignment - 1, sizeof(void *), &padding) ||
	    ir0_add_overflow(size, padding, &total_size))
		return NULL;

	raw_ptr = heap_alloc(total_size);
	if (!raw_ptr)
		return NULL;

	aligned_addr = ((uintptr_t)raw_ptr + sizeof(void *) + alignment - 1) &
		       ~(alignment - 1);
	((void **)aligned_addr)[-1] = raw_ptr;
	return (void *)aligned_addr;
}

/* A bad block is reported at the kfree call site, not inside the heap. */
static void kmem_free_or_panic(void *raw, const char *file, int line,
			       const char *caller)
{
	int ret = heap_free(raw);

	if (likely(ret == HEAP_BLOCK_OK))
		return;
	panicex(ret == HEAP_BLOCK_FREE ? "kfree: double free" :
					 "kfree: not a heap block",
		PANIC_MEM, file, line, caller);
}

void __kfree_checked(void *ptr, const char *file, int line, const char *caller)
{
	if (unlikely(!ptr))
		return;
	if (unlikely(!kmem_ptr_in_heap(ptr)))
		panicex("kfree: pointer out of heap range", PANIC_MEM, file, line, caller);
	kmem_free_or_panic(ptr, file, line, caller);
}

void __kfree_aligned_checked(void *ptr, const char *file, int line, const char *caller)
{
	if (unlikely(!ptr))
		return;
	if (unlikely(!kmem_ptr_in_heap(ptr)))
		panicex("kfree_aligned: pointer out of heap range", PANIC_MEM, file, line, caller);
	kmem_free_or_panic(((void **)ptr)[-1], file, line, caller);
}
