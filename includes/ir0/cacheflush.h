/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cacheflush.h
 * Description: Instruction-cache maintenance facade (Linux
 *              asm/cacheflush.h flush_icache_range()).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <asm/cacheflush.h>

/*
 * Nonzero when instructions the kernel stores through a data alias are not
 * fetched coherently until flush_icache_range() runs (ARM64). Zero on ISAs
 * whose instruction cache snoops stores (x86-64), so callers can skip the
 * work of mapping the range first.
 */
#define ICACHE_NEEDS_SYNC IR0_ARCH_ICACHE_NEEDS_SYNC

/*
 * Make instructions written to kernel VA [start, end) visible to instruction
 * fetch on every CPU in the inner shareable domain. No-op when
 * ICACHE_NEEDS_SYNC is 0.
 */
void flush_icache_range(uintptr_t start, uintptr_t end);
