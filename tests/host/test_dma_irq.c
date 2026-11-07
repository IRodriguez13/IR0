/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_dma_irq.c
 * Description: Host contracts for portable DMA alloc and IRQ dispatch.
 */

#include "test_harness.h"

#include <ir0/dma.h>
#include <ir0/errno.h>
#include <ir0/irq.h>
#include <stdint.h>

static uint32_t g_seen_irq;
static void *g_seen_data;

static void test_irq_fn(uint32_t irq, void *data)
{
	g_seen_irq = irq;
	g_seen_data = data;
}

void test_dma_irq_contract(void)
{
	uintptr_t phys = 0;
	void *va;
	int marker = 7;

	TEST_BEGIN("dma_alloc_rejects_overflow_and_zero");
	ASSERT(dma_alloc(0, &phys) == NULL);
	ASSERT(dma_alloc(64, NULL) == NULL);
	ASSERT(dma_alloc((size_t)IR0_DMA_MAX_BYTES + 1U, &phys) == NULL);
	TEST_END();

	TEST_BEGIN("dma_alloc_lifecycle");
	va = dma_alloc(64, &phys);
	ASSERT(va != NULL);
	ASSERT(phys != 0);
	dma_sync_for_device(va, 64);
	dma_sync_for_cpu(va, 64);
	ASSERT(dma_map_mmio(0x1000, 0x1000) == 0);
	dma_free(va, 64);
	TEST_END();

	TEST_BEGIN("irq_request_dispatch");
	g_seen_irq = 0;
	g_seen_data = NULL;
	ASSERT(irq_request(200, NULL, NULL, "bad") == -EINVAL);
	ASSERT(irq_request(200, test_irq_fn, &marker, "testdev") == 0);
	ASSERT(irq_request(200, test_irq_fn, &marker, "testdev") == -EBUSY);
	ASSERT(irq_dispatch(200) == 0);
	ASSERT_EQ(g_seen_irq, 200U);
	ASSERT(g_seen_data == &marker);
	ASSERT(irq_dispatch(201) == -ENOENT);
	TEST_END();
}
