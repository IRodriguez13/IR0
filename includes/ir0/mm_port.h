/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mm_port.h
 * Description: Opaque MM stats facade — no <mm/...> includes (impl in mm_port.c).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define IR0_MM_PAGE_SIZE 4096u

void ir0_mm_pmm_stats(size_t *total_frames, size_t *used_frames,
		      size_t *free_frames);
void ir0_mm_alloc_stats(size_t *total, size_t *used, size_t *allocs);

/* PMM-managed physical range [start, end) — for /proc/iomem honesty. */
uintptr_t ir0_mm_pmm_start(void);
uintptr_t ir0_mm_pmm_end(void);
