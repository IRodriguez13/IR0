/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mmu.h
 * Description: ARM64 boot MMU: 4-level 48-bit TTBR0 identity map, Device
 *              block registry and the supervisor half of every user root.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/arch_mm.h>
#include <stdint.h>

/* Build the boot identity map (DRAM + console UART) and enable the MMU. */
int arm64_mmu_enable(void);

/* 0 when the boot root is live in TTBR0 and SCTLR_EL1.M is set. */
int arm64_mmu_verify(void);

/* Physical address of the boot (kernel) root table. */
uintptr_t arm64_mmu_kernel_root(void);

/*
 * Map every 2 MiB Device-nGnRnE identity block intersecting [pa, pa+size)
 * in the boot root and record it, so later user roots inherit it.
 */
int arm64_mmu_map_device_range(uint64_t pa, uint64_t size);
int arm64_mmu_map_device_block(uint64_t pa);

/*
 * Install the supervisor identity map (DRAM window and recorded Device
 * blocks) in a user root built by the common page-table walker.
 */
int arm64_mmu_map_supervisor(address_space_root_t root);

/*
 * Nonzero when [va, va + size) meets the supervisor identity of the user
 * roots: the DRAM window or a Device block recorded so far.
 */
int arm64_mmu_supervisor_overlaps(uintptr_t va, size_t size);

/* Add Device blocks recorded after @root was created. Idempotent. */
int arm64_mmu_sync_supervisor(address_space_root_t root);
