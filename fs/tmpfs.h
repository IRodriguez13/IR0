/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: tmpfs.h
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - TMPFS (Temporary Filesystem) Header
 * Copyright (C) 2025  Iván Rodriguez
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <ir0/stat.h>
#include <ir0/types.h>

struct vfs_dirent;

/* TMPFS API functions for VFS integration */
bool tmpfs_is_available(void);
int tmpfs_stat(const char *path, stat_t *buf);
int tmpfs_mkdir(const char *path, mode_t mode);
int tmpfs_create_file(const char *path, mode_t mode);
int tmpfs_read_file(const char *path, void *buf, size_t count, size_t *read_count, off_t offset);
int tmpfs_write_file(const char *path, const void *buf, size_t count, size_t *written_count, off_t offset);
int tmpfs_unlink(const char *path);
int tmpfs_rmdir(const char *path);
int tmpfs_chown(const char *path, uid_t owner, gid_t group);
int tmpfs_chmod(const char *path, mode_t mode);
int tmpfs_truncate(const char *path, size_t length);
int tmpfs_readdir(const char *path, struct vfs_dirent *entries, int max_entries);

/* Filesystem registration */
int tmpfs_register(void);
