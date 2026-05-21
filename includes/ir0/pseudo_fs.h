/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: pseudo_fs.h
 * Description: Registered ops table for /proc and /sys pseudo filesystems.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <ir0/stat.h>
#include <ir0/types.h>

#define PSEUDO_FS_PROC_FD_BASE 1500
#define PSEUDO_FS_SYS_FD_BASE  3500
#define PSEUDO_FS_DYN_FD_BASE  1700
#define PSEUDO_FS_MAX_ENTRIES  64
#define PSEUDO_FS_MAX_DYNAMIC  16

typedef int (*pseudo_fs_dynamic_match_fn)(const char *full_path, void **out_ctx);

typedef struct pseudo_fs_ops
{
    int64_t (*read)(void *ctx, char *buf, size_t count, off_t *offset);
    int64_t (*write)(void *ctx, const char *buf, size_t count);
    int64_t (*open)(void *ctx, int flags);
    int64_t (*close)(void *ctx);
    int (*stat)(void *ctx, stat_t *st);
} pseudo_fs_ops_t;

typedef struct pseudo_fs_entry
{
    char full_path[256];
    const pseudo_fs_ops_t *ops;
    void *ctx;
    int fd;
    int in_use;
} pseudo_fs_entry_t;

int pseudo_fs_register(const char *mount_prefix, const char *rel_path,
                       const pseudo_fs_ops_t *ops, void *ctx);
int pseudo_fs_register_dynamic(const char *mount_prefix,
                               pseudo_fs_dynamic_match_fn match,
                               const pseudo_fs_ops_t *ops);
const pseudo_fs_entry_t *pseudo_fs_lookup(const char *full_path);
const pseudo_fs_entry_t *pseudo_fs_find_by_fd(int fd);
int pseudo_fs_proc_init(void);
int pseudo_fs_sys_init(void);
void pseudo_fs_reset(void);
void pseudo_fs_nodes_register_all(void);

int64_t pseudo_fs_read_fd(int fd, char *buf, size_t count, off_t offset);
int64_t pseudo_fs_write_fd(int fd, const char *buf, size_t count);
int64_t pseudo_fs_open_path(const char *full_path, int flags, int *out_fd);
int64_t pseudo_fs_close_fd(int fd);
int pseudo_fs_stat_path(const char *full_path, stat_t *st);
