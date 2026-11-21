/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: va_search.h
 * Description: Free user virtual address search for mmap placement owned by mm/.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <ir0/mm.h>

struct process;
typedef struct process process_t;

/*
 * Linux-like top-down placement for mmap(NULL) and non-fixed hints.
 * Updates process_mmap_base(proc) to the chosen start for the next call.
 */
uintptr_t mm_pick_free_va_topdown(process_t *proc, address_space_root_t root,
				  size_t length);
/*
 * Non-zero @hint: returned only if page-aligned, inside the mmap window and
 * unmapped, else 0. Zero @hint: top-down placement.
 */
uintptr_t mm_find_free_va(address_space_root_t root, process_t *proc,
			  uintptr_t hint, size_t length);
