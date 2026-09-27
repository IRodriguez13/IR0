/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_mm.h
 * Description: ISA MM layout + PTE/translation facades (portable paging).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/mm.h>

/* Kernel heap region supplied by the active architecture memory layout. */
uintptr_t mm_kernel_heap_start(void);
size_t mm_kernel_heap_size(void);

void mm_enable_translation(void);
int mm_translation_enabled(void);
int mm_translation_ready(void);

/*
 * Translation-table indices from the root level to the leaf-table level.
 * The selected ISA backend defines how the virtual address is decomposed.
 */
void mm_va_indices(uintptr_t va, size_t idx[4]);

int mm_pte_present(uint64_t e);
int mm_pte_large(uint64_t e);
uintptr_t mm_pte_phys(uint64_t e);

uint64_t mm_make_table_pte(uintptr_t phys, int user);
uint64_t mm_make_leaf_pte(uintptr_t phys, uint64_t flags12, int exec);
void mm_pte_set_user(uint64_t *e);
