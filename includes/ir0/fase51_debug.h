/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fase51_debug.h
 * Description: FASE51 shell bring-up serial diagnostics (gated)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once


#include <config.h>
#include <stddef.h>
#include <stdint.h>

#if CONFIG_DEBUG_FASE51
#define IR0_FASE51_DBG 1
#else
#define IR0_FASE51_DBG 0
#endif

void fase51_dbg_pipe2(int read_fd, int write_fd, int flags, int64_t ret);
void fase51_dbg_dup2(int oldfd, int newfd, int64_t ret);
void fase51_dbg_close(int fd, int is_pipe, int64_t ret);
void fase51_dbg_wait4(int64_t pid, int64_t ret);
void fase51_dbg_pipe_rw(const char *op, int fd, int64_t ret);
void fase51_dbg_open_redirect(const char *path, int linux_flags, int64_t ret);
void fase51_dbg_exec_argv(const char *path, const char *argv0, const char *argv1);
void fase51_dbg_wait_wake(uint32_t parent_pid, uint32_t child_pid, int *status_ptr,
			  int status_val, int copy_ret);

