/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: direct_map.h
 * Description: Boot-time direct map of all RAM (see <ir0/page.h>).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/*
 * Map every RAM bank of the boot inventory at PAGE_OFFSET, kernel-only and
 * non-executable, then let the CPU walk it. Runs once, after the boot
 * memory snapshot and before anything calls phys_to_virt().
 */
int direct_map_init(void);
