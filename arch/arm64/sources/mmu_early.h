/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mmu_early.h
 * Description: ARM64 QEMU virt early identity-map MMU enable (TTBR0 only).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/**
 * Build identity page tables (DRAM + UART MMIO) and enable the EL1 MMU.
 * Returns 0 on success, negative errno-style on failure.
 */
int arm64_mmu_early_enable(void);
