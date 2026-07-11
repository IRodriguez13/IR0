/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscalls_glue.h
 * Description: IR0 kernel header — syscalls glue
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/process.h>
#include <stddef.h>
#include <stdint.h>
#include <ir0/pipe.h>
#include "validate_user.h"

fd_entry_t *get_process_fd_table(void);
void ensure_devfs_init(void);
int stdio_is_redirected(fd_entry_t *fd_table, int fd);
int pipe_wait(process_t *proc, pipe_t *pipe, int waiting_read);
void fase48_note_fd_created(void);

/* Resolve user path against dirfd (Linux openat/fstatat subset). */
int ir0_resolve_path_at(int dirfd, const char *user_path, char *resolved,
                        size_t resolved_sz);

/* Unredirected stdin read path (keyboard wait + copy to user). */
int64_t syscalls_read_stdio_stdin(void *buf, size_t count);
