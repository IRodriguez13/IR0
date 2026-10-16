/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: early_mm.h
 * Description: Staged brk/mmap arena installed per loaded ELF image
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <ir0/syscall_table.h>

struct early_mm_layout
{
	uint64_t heap_base;
	uint64_t map_base;
	uint64_t map_limit;
};

#define IR0_EARLY_MCL_CURRENT 1
#define IR0_EARLY_MCL_FUTURE 2
#define IR0_EARLY_MCL_ONFAULT 4
#define IR0_EARLY_MCL_KNOWN \
	(IR0_EARLY_MCL_CURRENT | IR0_EARLY_MCL_FUTURE | IR0_EARLY_MCL_ONFAULT)
#define IR0_EARLY_MLOCK_ONFAULT 1
#define IR0_EARLY_SEAL_MAX 4

extern const struct syscall_context_provider early_mm_provider;

/* Install the arena for the image about to enter EL0. Copies layout. */
void early_mm_activate(const struct early_mm_layout *layout);
