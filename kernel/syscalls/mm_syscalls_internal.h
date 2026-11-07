/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mm_syscalls_internal.h
 * Description: Internal facade of the memory syscall units (mm_syscalls.c,
 *              mm_vma.c, mm_brk.c, swap_syscalls.c). Not a public API.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define SYSCALL_PTR_ERR(err) ((void *)(intptr_t)(-(err)))
#define MMAP_AUDIT_FAILED ((void *)(intptr_t)-1)

/* VMA list of the current process (mm_vma.c). */
int mm_detach_vma_range(uintptr_t start, uintptr_t end);
int mm_apply_vma_prot(uintptr_t start, uintptr_t end, int prot);
int mm_prepare_map_fixed(uintptr_t start, size_t length);
