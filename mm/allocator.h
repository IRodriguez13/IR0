/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: allocator.h
 * Description: mm-private kernel heap backend (first-fit free list with
 *              boundary tags). Everything outside mm/ uses <ir0/kmem.h>.
 */

#pragma once

#include <stddef.h>
#include <ir0/arch_mm.h>

/* The heap spans the range the architecture reserves via <ir0/arch_mm.h>. */
#define SIMPLE_HEAP_START (mm_kernel_heap_start())
#define SIMPLE_HEAP_SIZE (mm_kernel_heap_size())
#define SIMPLE_HEAP_END (SIMPLE_HEAP_START + SIMPLE_HEAP_SIZE)

/* heap_init() is public and declared in <ir0/kmem.h>. */
void *heap_alloc(size_t size);
void heap_free(void *ptr);

/* heap_block_check() results. */
#define HEAP_BLOCK_OK 0
#define HEAP_BLOCK_BAD (-1)  /* not the start of a heap block */
#define HEAP_BLOCK_FREE (-2) /* a block that is already free */

/* Classify @ptr without freeing it or panicking. */
int heap_block_check(const void *ptr);
void heap_stats(size_t *total, size_t *used, size_t *allocs);
void heap_coalesce_stats(size_t *forward, size_t *backward);

/*
 * Only the test kernel defines this hook; heap_alloc calls it from inside
 * its critical section so a ktest can prove the section masks IRQs.
 */
void heap_locked_test_hook(void) __attribute__((weak));
