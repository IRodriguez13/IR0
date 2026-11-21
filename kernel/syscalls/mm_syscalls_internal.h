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
 *              swap_syscalls.c). VMA operations are declared in mm/mmap.h.
 *              Not a public API.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <mm/mmap.h>

#define SYSCALL_PTR_ERR(err) ((void *)(intptr_t)(-(err)))
#define MMAP_AUDIT_FAILED ((void *)(intptr_t)-1)
