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

/* Forward declaration */
typedef struct tmpfs_inode tmpfs_inode_t;

/* TMPFS API functions for VFS integration */
bool tmpfs_is_available(void);
tmpfs_inode_t *tmpfs_find_inode(const char *path);
uint32_t tmpfs_get_inode_number(const char *path);
int tmpfs_stat(const char *path, stat_t *buf);
int tmpfs_mkdir(const char *path, mode_t mode);
int tmpfs_create_file(const char *path, mode_t mode);
int tmpfs_read_file(const char *path, void *buf, size_t count, size_t *read_count, off_t offset);
int tmpfs_write_file(const char *path, const void *buf, size_t count, size_t *written_count, off_t offset);
int tmpfs_unlink(const char *path);
int tmpfs_rmdir(const char *path);
int tmpfs_readdir(const char *path, struct vfs_dirent_readdir *entries, int max_entries);

/* Filesystem registration */
int tmpfs_register(void);

