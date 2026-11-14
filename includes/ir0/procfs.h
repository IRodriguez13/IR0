/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: procfs.h
 * Description: IR0 kernel source/header file
 */

// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Simple /proc filesystem
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: procfs.h
 * Description: Minimal /proc filesystem - on-demand, no mounting
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <ir0/stat.h>
#include <ir0/types.h>
#include <ir0/vfs.h>

/* /proc filesystem interface */
bool is_proc_path(const char *path);
int proc_stat(const char *path, stat_t *st);
int proc_access_path(const char *path, uid_t euid);
int proc_pid_get_owner(pid_t pid, uid_t *uid, gid_t *gid);

/* Path-based readdir for /proc, /proc/pid, /proc/pid/N (vfs_dirent batch). */
int proc_readdir(const char *path, struct vfs_dirent *entries, int max_entries);

/* Path resolution and the readers the in-kernel procfs tests call. */
int proc_pid_environ_read(char *buf, size_t count, pid_t pid);
int proc_readlink(const char *path, char *buf, size_t buflen);
const char *proc_resolve_path(const char *path, pid_t *pid_out);
int proc_is_virtual_subdir(const char *path);
