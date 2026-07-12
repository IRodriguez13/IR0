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
 * After MMU on: map one 4 KiB Normal page at @pa with EL0 RW (AP=01).
 * Splits the DRAM 1 GiB block into L2/L3 as needed. Does not use EL0 on 1 GiB.
 * Returns 0 on success, negative on error.
 */
int arm64_mmu_map_user_page(uint64_t pa);

/** True if @va..@va+@len-1 lies entirely in the mapped EL0 user page. */
int arm64_mmu_user_buf_ok(uint64_t va, uint64_t len);

/**
 * Verify TTBR0 points at early L1 and SCTLR_EL1.M is set.
 * Returns 0 if OK, negative otherwise.
 */
int arm64_mmu_early_verify(void);
