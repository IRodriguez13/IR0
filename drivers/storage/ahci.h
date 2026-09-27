/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ahci.h
 * Description: AHCI SATA host — probe, block backend, per-space MMIO mapping.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/mm.h>
#include <stdint.h>

void ahci_probe(void);

/* Map ABAR into @root (supervisor, cache-disable). No-op if not probed. */
void ahci_map_mmio_in_directory(address_space_root_t root);

int ahci_disk_present(void);
uint64_t ahci_sector_count(void);
