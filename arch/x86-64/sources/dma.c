/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: dma.c
 * Description: x86-64 coherent DMA (identity-mapped heap, no cache maintenance).
 */

#include <ir0/dma.h>
#include <ir0/irq.h>
#include <ir0/kmem.h>
#include <stdint.h>
#include <string.h>

void *dma_alloc(size_t size, uintptr_t *phys)
{
	void *va;

	if (!phys || size == 0 || size > IR0_DMA_MAX_BYTES)
		return NULL;
	if (size > SIZE_MAX - 15U)
		return NULL;
	va = kmalloc_aligned((size + 15U) & ~15U, 16);
	if (!va)
		return NULL;
	memset(va, 0, size);
	*phys = (uintptr_t)va;
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
	(void)va;
	(void)size;
}

void dma_sync_for_cpu(void *va, size_t size)
{
	(void)va;
	(void)size;
}

int dma_map_mmio(uintptr_t pa, size_t size)
{
	(void)pa;
	(void)size;
	return 0;
}

int irq_hw_enable(uint32_t irq)
{
	(void)irq;
	return 0;
}
