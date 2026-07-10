/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ahci.h
 * Description: AHCI PCI detect entry.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

void ahci_probe(void);
