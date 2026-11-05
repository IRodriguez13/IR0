/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_mock_mm_port.h
 * Description: Test-only MM stats facade mock control surface.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

void ktm_mm_port_mock_reset(void);

void ktm_mm_port_mock_set_pmm_stats(size_t total_frames, size_t used_frames,
				    size_t free_frames);
void ktm_mm_port_mock_set_alloc_stats(size_t total, size_t used, size_t allocs);
void ktm_mm_port_mock_set_range(uintptr_t start, uintptr_t end);

int ktm_mm_port_mock_pmm_stats_calls(void);
int ktm_mm_port_mock_alloc_stats_calls(void);
