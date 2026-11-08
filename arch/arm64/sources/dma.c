/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: dma.c
 * Description: ARM64 DMA with cache clean/invalidate; identity-mapped buffers.
 */

#include "irq_backend.h"
#include "mmu.h"

#include <ir0/dma.h>
#include <ir0/errno.h>
#include <ir0/irq.h>
#include <ir0/kmem.h>
#include <string.h>

static void cache_clean(uintptr_t start, size_t size)
{
	uintptr_t addr;
	uintptr_t end = start + size;

	if (!size || start > end)
		return;
	addr = start & ~(uintptr_t)63U;
	while (addr < end)
	{
		__asm__ volatile("dc cvac, %0" : : "r"(addr) : "memory");
		addr += 64U;
	}
	__asm__ volatile("dsb sy" ::: "memory");
}

static void cache_invalidate(uintptr_t start, size_t size)
{
	uintptr_t addr;
	uintptr_t end = start + size;

	if (!size || start > end)
		return;
	addr = start & ~(uintptr_t)63U;
	while (addr < end)
	{
		__asm__ volatile("dc ivac, %0" : : "r"(addr) : "memory");
		addr += 64U;
	}
	__asm__ volatile("dsb sy" ::: "memory");
}

void *dma_alloc(size_t size, uintptr_t *phys)
{
	void *va;

	if (!phys || size == 0 || size > IR0_DMA_MAX_BYTES)
		return NULL;
	if (size > SIZE_MAX - 63U)
		return NULL;
	va = kmalloc_aligned((size + 63U) & ~63U, 64);
	if (!va)
		return NULL;
	memset(va, 0, size);
	*phys = (uintptr_t)va;
	dma_sync_for_device(va, size);
	return va;
}

void dma_free(void *va, size_t size)
{
	(void)size;
	if (va)
		kfree_aligned(va);
}

void dma_sync_for_device(void *va, size_t size)
{
	if (va)
		cache_clean((uintptr_t)va, size);
}

void dma_sync_for_cpu(void *va, size_t size)
{
	if (va)
		cache_invalidate((uintptr_t)va, size);
}

int dma_map_mmio(uintptr_t pa, size_t size)
{
	if (!pa || size == 0)
		return -EINVAL;
	return arm64_mmu_map_device_range((uint64_t)pa, (uint64_t)size);
}

int irq_hw_enable(uint32_t irq)
{
	return arm64_irq_backend_enable(irq);
}
