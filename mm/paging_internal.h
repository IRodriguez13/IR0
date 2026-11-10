/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: paging_internal.h
 * Description: Internal facade shared by the mm/paging*.c units (page-table
 *              core, user copies, fork, identity maps, accounting). Not a
 *              public API: other subsystems use <ir0/paging.h> and other ir0/ facades.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <ir0/arch_mm.h>

typedef enum
{
    IR0_MM_FRAME_UNKNOWN = 0,
    IR0_MM_FRAME_USER = 1,
    IR0_MM_FRAME_PT = 2,
    IR0_MM_FRAME_KERNEL = 3
} ir0_mm_frame_type_t;

/* Page-table and frame accounting (paging_audit.c). */
extern uint64_t ir0_mm_root_created;
extern uint64_t ir0_mm_root_freed;
extern uint64_t ir0_mm_level1_created;
extern uint64_t ir0_mm_level1_freed;
extern uint64_t ir0_mm_level2_created;
extern uint64_t ir0_mm_level2_freed;
extern uint64_t ir0_mm_level3_created;
extern uint64_t ir0_mm_level3_freed;
extern uint64_t ir0_mm_leaf_created;
extern uint64_t ir0_mm_leaf_freed;
extern uint64_t ir0_mm_frame_user_alloc;
extern uint64_t ir0_mm_frame_user_free;
extern uint64_t ir0_mm_frame_pt_alloc;
extern uint64_t ir0_mm_frame_pt_free;
extern uint64_t ir0_mm_frame_kernel_alloc;
extern uint64_t ir0_mm_frame_kernel_free;

ir0_mm_frame_type_t ir0_mm_get_frame_type(uint64_t phys_addr);
void ir0_mm_set_frame_type(uint64_t phys_addr, ir0_mm_frame_type_t t);
void ir0_mm_log_frame_type(const char *kind, uint64_t frame,
			   ir0_mm_frame_type_t t);
/* Page-table core (paging.c). */
void ir0_mm_free_table_frame(uint64_t frame, int level);
uint64_t *paging_existing_table(uint64_t *table, size_t index);
uint64_t *paging_get_or_create_table(address_space_root_t root,
				      uint64_t *parent, size_t index,
				      int create, uint64_t map_flags,
				      int level);

/* Switch to the pinned kernel root for physical-address access. */
void paging_phys_map_begin(uintptr_t *saved_root);
void paging_phys_map_end(uintptr_t saved_root);


/*
 * Linux mm/x86: pte_pfn(), pud_page() — extract the physical pointer from a
 * descriptor without software/NX bits (via mm_pte_phys).
 */
static inline uintptr_t paging_entry_pfn(uint64_t entry)
{
	return mm_pte_phys(entry);
}

static inline int paging_entry_large(uint64_t entry)
{
	return mm_pte_large(entry);
}

static inline uint64_t *paging_entry_table(uint64_t entry)
{
	if (!mm_pte_present(entry))
		return NULL;
	if (paging_entry_large(entry))
		return NULL;
	return (uint64_t *)paging_entry_pfn(entry);
}
