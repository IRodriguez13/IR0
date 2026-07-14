/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mmu_early.h
 * Description: ARM64 early TTBR0 identity map + Device/user page API (F7c).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/** Build identity page tables (DRAM + UART) and enable EL1 MMU. */
int arm64_mmu_early_enable(void);

/**
 * After MMU on: map a 2 MiB Device-nGnRnE identity block covering @pa.
 * Returns 0 on success, negative on error.
 */
int arm64_mmu_map_device_block(uint64_t pa);

/**
 * After MMU on: map one 4 KiB Normal page at @pa with EL0 RW (AP=01), UXN.
 * Splits the DRAM 1 GiB block into L2/L3 as needed. Does not use EL0 on 1 GiB.
 * Returns 0 on success, negative on error.
 */
int arm64_mmu_map_user_page(uint64_t pa);

/**
 * Like arm64_mmu_map_user_page, but @exec_el0 non-zero clears UXN (EL0 execute).
 * Accumulates pages within the same 2 MiB via an L3 pool.
 */
int arm64_mmu_map_user_page_flags(uint64_t pa, int exec_el0);

/** True if @va..@va+@len-1 lies entirely in mapped EL0 user pages. */
int arm64_mmu_user_buf_ok(uint64_t va, uint64_t len);

/**
 * Verify TTBR0 points at early L1 and SCTLR_EL1.M is set.
 * Returns 0 if OK, negative otherwise.
 */
int arm64_mmu_early_verify(void);

/**
 * F7i: activate a cloned TTBR0 L1 (same maps), verify, restore original.
 * Returns 0 on success.
 */
int arm64_mmu_ttbr_dual_smoke(void);

/** Physical/VA of early L1 roots (identity-mapped). Valid after MMU enable. */
uint64_t arm64_mmu_root_a(void);
uint64_t arm64_mmu_root_b(void);

/** Ensure l1_table_b is a clone of l1_table (idempotent). */
void arm64_mmu_clone_root_b(void);
