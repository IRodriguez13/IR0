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


void *kmalloc(size_t size)
{
    return alloc(size);
}

void kfree(void *ptr)
{
    alloc_free(ptr);
}

void *krealloc(void *ptr, size_t size)
{
    return all_realloc(ptr, size);
}

void heap_init(void)
{
    alloc_init();
}
