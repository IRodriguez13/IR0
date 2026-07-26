/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mm_struct.h
 * Description: Shared address-space object (refcount) for fork/CLONE_VM.
 *
 * Ownership:
 *   - mm_create() returns refcount=1
 *   - mm_get() / mm_put() balance shares (CLONE_VM)
 *   - Last mm_put() tears down page tables + VMA list when owns_tables
 * May sleep: no (destroy may free; callers must not hold IRQ-only locks).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <ir0/types.h>

struct mmap_region;
struct process;

typedef struct mm_struct
{
	int refcount;
	uint64_t *page_directory;
	uint8_t owns_tables;
	struct mmap_region *mmap_list;
	uint64_t mmap_base;
	uint64_t heap_start;
	uint64_t heap_end;
	uint64_t stack_start;
	uint64_t stack_size;
} mm_struct_t;

/* Allocate empty mm (refcount=1). Returns NULL on OOM. */
mm_struct_t *mm_create(void);

/* Bump refcount. Returns mm or NULL. */
mm_struct_t *mm_get(mm_struct_t *mm);

/* Drop refcount; destroy on zero. */
void mm_put(mm_struct_t *mm);

/*
 * Bind process private mirrors to @mm (process_pgd / owns / heap cursors).
 * Callers outside mm_struct.c must use process_pgd() / process_mm_owns_tables().
 */
void process_mm_bind(struct process *p, mm_struct_t *mm);

/* Share parent's mm with child (CLONE_VM). Returns 0 or -errno. */
int process_mm_share(struct process *child, struct process *parent);

/* Sync process mirrors FROM mm (after share/bind). */
void process_mm_sync_to_process(struct process *p);

/* Sync mm FROM process mirrors (before mm_put / after mutating mmap_list/heap). */
void process_mm_sync_from_process(struct process *p);

/* Set mmap_list on both process and mm (if present). */
void process_mm_set_mmap_list(struct process *p, struct mmap_region *list);

/* 1 if sole owner of page tables / may free via mm_put or legacy teardown. */
int process_mm_owns_tables(const struct process *p);
