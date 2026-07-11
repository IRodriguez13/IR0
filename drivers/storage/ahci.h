/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ahci.h
 * Description: AHCI SATA host — probe, block backend, MMIO map for process CR3.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

void ahci_probe(void);

/* Identity-map ABAR into @pml4 (supervisor, cache-disable). No-op if not probed. */
void ahci_map_mmio_in_directory(uint64_t *pml4);

int ahci_disk_present(void);
uint64_t ahci_sector_count(void);
