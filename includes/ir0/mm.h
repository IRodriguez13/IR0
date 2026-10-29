/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mm.h
 * Description: ISA root-table layout helpers (user vs kernel half).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

/* Opaque-by-contract root of a process translation-table hierarchy. */
typedef uint64_t *address_space_root_t;

/* Number of root translation-table slots assigned to user mappings. */
unsigned mm_user_root_slots(void);

/* Total root-table slots (x86-64: 512). */
unsigned mm_root_slots(void);

/*
 * True when @index is a root slot owned by the process.  Never compare
 * against a hardcoded 256: x86-64 splits the root 256/256, but ARM64 owns
 * the whole 512-entry TTBR0 L0, and a stale level1 table in the upper
 * slots would never be reclaimed on unmap.
 */
static inline int mm_user_root_slot(unsigned index)
{
	return index < mm_user_root_slots();
}

/*
 * Copy present kernel-half entries from @src_root into @dst_root.
 * User half of @dst_root is left untouched by this helper.
 */
void mm_copy_kernel_half(address_space_root_t dst_root,
			 const uint64_t *src_root);

/*
 * ISA userspace VA window for access_ok / is_user_address (no page walk).
 * Returns 1 if [addr, addr+size) is a canonical user range for this arch.
 */
int mm_user_va_ok(uintptr_t addr, size_t size);
