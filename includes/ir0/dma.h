/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: dma.h
 * Description: Portable DMA allocation and cache sync (explicit physical address).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define IR0_DMA_MAX_BYTES (2U * 1024U * 1024U)

void *dma_alloc(size_t size, uintptr_t *phys);
void dma_free(void *va, size_t size);
void dma_sync_for_device(void *va, size_t size);
void dma_sync_for_cpu(void *va, size_t size);
int dma_map_mmio(uintptr_t pa, size_t size);
