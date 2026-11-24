/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mmu.h
 * Description: ARM64 kernel MMU: boot DRAM identity in TTBR0 until the
 *              first process; the kernel itself lives in TTBR1.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <asm/pgtable.h>
#include <stdint.h>

/* Install the boot DRAM identity map in TTBR0 (entry.S enabled the MMU). */
int arm64_mmu_enable(void);

/* 0 when the boot root is live in TTBR0 and SCTLR_EL1.M is set. */
int arm64_mmu_verify(void);

/* Physical address of the boot (kernel) root table. */
uintptr_t arm64_mmu_kernel_root(void);
