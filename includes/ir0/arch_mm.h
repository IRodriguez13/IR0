/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_mm.h
 * Description: ISA MM layout + PTE/translation facades (portable paging).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/mm.h>

/* ISA-neutral mapping policy passed between common MM and the backend. */
#define IR0_MM_MAP_PRESENT       (1ULL << 0)
#define IR0_MM_MAP_WRITE         (1ULL << 1)
#define IR0_MM_MAP_USER          (1ULL << 2)
#define IR0_MM_MAP_WRITETHROUGH  (1ULL << 3)
#define IR0_MM_MAP_NOCACHE       (1ULL << 4)
#define IR0_MM_MAP_ACCESSED      (1ULL << 5)
#define IR0_MM_MAP_DIRTY         (1ULL << 6)
#define IR0_MM_MAP_LARGE         (1ULL << 7)
#define IR0_MM_MAP_GLOBAL        (1ULL << 8)
#define IR0_MM_MAP_COW           (1ULL << 9)
#define IR0_MM_MAP_EXEC          (1ULL << 52)

/* Kernel heap region supplied by the active architecture memory layout. */
uintptr_t mm_kernel_heap_start(void);
size_t mm_kernel_heap_size(void);

/* Inclusive userspace virtual-address window for the selected ISA policy. */
uintptr_t mm_user_va_start(void);
uintptr_t mm_user_va_end(void);

void mm_enable_translation(void);
int mm_translation_enabled(void);
int mm_translation_ready(void);

/* Whether the backend can encode the common large identity-map leaf. */
int mm_large_identity_supported(void);

/*
 * Translation-table indices from the root level to the leaf-table level.
 * The selected ISA backend defines how the virtual address is decomposed.
 */
void mm_va_indices(uintptr_t va, size_t idx[4]);

/* Translation-table geometry used by common walkers. */
size_t mm_table_entries(void);
uint64_t mm_large_leaf_pages(unsigned int level);
uintptr_t mm_va_from_indices(const size_t idx[4]);

int mm_pte_present(uint64_t e);
int mm_pte_large(uint64_t e);
int mm_pte_executable(uint64_t e);
uintptr_t mm_pte_phys(uint64_t e);
uint64_t mm_pte_mapping_flags(uint64_t e);
void mm_pte_mark_cow(uint64_t *e);

uint64_t mm_make_table_pte(uintptr_t phys, int user);
uint64_t mm_make_leaf_pte(uintptr_t phys, uint64_t flags12, int exec);
void mm_pte_set_user(uint64_t *e);

/*
 * Create an empty process translation root with the supervisor mappings that
 * the selected ISA requires while a task is running on that root.  The common
 * process lifecycle owns the resulting root; descriptor layout and bootstrap
 * mappings remain an ISA backend responsibility.
 *
 * A backend that cannot yet provide isolated process roots must return NULL.
 * Callers treat that as allocation failure rather than reusing a global root.
 */
address_space_root_t mm_create_user_root(void);

/* Retire a root returned by mm_create_user_root() after common unmap work. */
void mm_destroy_user_root(address_space_root_t root);

/* Prepare ISA-private shared mappings before a root becomes current. */
void mm_prepare_root_activation(address_space_root_t root);

/*
 * Optional backend-owned user mapping. Return -EOPNOTSUPP when @root belongs
 * to the generic walker; otherwise return 0 or a negative errno.
 */
int mm_backend_map_user_page(address_space_root_t root, uintptr_t va,
			     uintptr_t pa, uint64_t flags);
int mm_backend_unmap_user_page(address_space_root_t root, uintptr_t va);

/*
 * Resolve an owned user leaf without exposing translation-table layout to the
 * generic uaccess path. Return -EOPNOTSUPP for generic-walker roots.
 */
int mm_backend_lookup_user_page(address_space_root_t root, uintptr_t va,
				uintptr_t *phys, uint64_t *flags);

/* Apply final user permissions to backend-owned leaves. */
int mm_backend_protect_user_range(address_space_root_t root, uintptr_t start,
				  size_t length, uint64_t flags);

/*
 * Retire user frames owned by an ISA-private translation root.  Backends must
 * return -EOPNOTSUPP for roots handled by the common table walker.
 */
int mm_backend_reclaim_user_frames(address_space_root_t root,
				   uint64_t *reclaimed_pages);
