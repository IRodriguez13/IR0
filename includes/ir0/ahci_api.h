/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ahci_api.h
 * Description: AHCI facade for MM (process CR3) without drivers/ includes.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef _IR0_AHCI_API_H
#define _IR0_AHCI_API_H

#include <stdint.h>

void ahci_map_mmio_in_directory(uint64_t *pml4);

#endif /* _IR0_AHCI_API_H */
