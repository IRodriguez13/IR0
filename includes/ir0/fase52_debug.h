/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fase52_debug.h
 * Description: FASE52 TCC bring-up serial diagnostics (gated)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once


#include <config.h>
#include <stddef.h>
#include <stdint.h>

#if CONFIG_DEBUG_FASE52
#define IR0_FASE52_DBG 1
#else
#define IR0_FASE52_DBG 0
#endif

void fase52_dbg_brk(void *addr, int64_t ret);
void fase52_dbg_mmap(void *addr, size_t len, int prot, int flags, int fd, int64_t off,
		     void *ret);
void fase52_dbg_munmap(void *addr, size_t len, int64_t ret);
void fase52_dbg_mprotect(void *addr, size_t len, int prot, int64_t ret);
void fase52_dbg_openat(int dfd, const char *path, int flags, int64_t ret);
void fase52_dbg_rw(const char *op, int fd, int64_t ret);
void fase52_dbg_lseek(int fd, int64_t off, int whence, int64_t ret);
void fase52_dbg_stat_path(const char *path, int64_t ret);
void fase52_dbg_access(const char *path, int mode, int64_t ret);
void fase52_dbg_exec_argv(const char *path, const char *argv0, const char *argv1,
			  uint64_t env_count);

