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
 * 4-level VA indices (9 bits each) for 4 KiB granules — x86-64 and aarch64.
 * idx[0]=L0/PML4, idx[1]=L1/PDPT, idx[2]=L2/PD, idx[3]=L3/PT.
 */
void mm_va_indices(uintptr_t va, size_t idx[4]);

int mm_pte_present(uint64_t e);
int mm_pte_large(uint64_t e);
uintptr_t mm_pte_phys(uint64_t e);

uint64_t mm_make_table_pte(uintptr_t phys, int user);
uint64_t mm_make_leaf_pte(uintptr_t phys, uint64_t flags12, int exec);
void mm_pte_set_user(uint64_t *e);
