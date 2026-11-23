/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: map_sys.h
 * Description: mmap(2), munmap(2) and mprotect(2) owned by mm/.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <ir0/types.h>

struct process;
typedef struct process process_t;

void *mm_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int mm_munmap(void *addr, size_t length);
int mm_mprotect(void *addr, size_t len, int prot);
int mm_msync(void *addr, size_t len, int flags);
void *mm_mmap_file_private(process_t *proc, void *addr, size_t length, int prot,
                           int flags, int fd, off_t offset);
