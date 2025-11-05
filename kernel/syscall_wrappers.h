// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscall_wrappers.h
 * Description: Safe wrapper functions for system calls
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "process.h"

#define MAX_FDS 32

/* Safe syscall wrappers */
int64_t safe_sys_write(int fd, const void *buf, size_t count);
int64_t safe_sys_read(int fd, void *buf, size_t count);
int64_t safe_sys_open(const char *pathname, int flags, mode_t mode);
int64_t safe_sys_mkdir(const char *pathname, mode_t mode);

/* Safe process management wrappers */
process_t *safe_process_create(const char *name, void *entry_point);

/* Safe memory management wrappers */
void *safe_kmalloc(size_t size);