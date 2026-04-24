/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vfs.h
 * Description: IR0 kernel source/header file
 */

/* vfs.h - Minimal path-based Virtual File System */
#pragma once

#include <ir0/stat.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ir0/types.h>

struct vfs_dirent;

#define VFS_PATH_MAX 256
#define VFS_NAME_MAX 255

/*
 * VFS operations table — every filesystem implements this.
 * All operations are path-based; VFS does not track inodes.
 */
struct vfs_ops {
    int (*stat)(const char *path, stat_t *buf);
    int (*mkdir)(const char *path, mode_t mode);
    int (*create)(const char *path, mode_t mode);
    int (*unlink)(const char *path);
    int (*rmdir)(const char *path);
    int (*link)(const char *oldpath, const char *newpath);
    int (*chown)(const char *path, uid_t owner, gid_t group);
    int (*chmod)(const char *path, mode_t mode);
    int (*readdir)(const char *path, struct vfs_dirent *entries, int max);
    int (*read)(const char *path, void *buf, size_t count,
                size_t *bytes_read, off_t offset);
    int (*write)(const char *path, const void *buf, size_t count,
                 size_t *bytes_written, off_t offset);
    int (*truncate)(const char *path, size_t length);
};

/* Filesystem type descriptor — one per FS driver */
struct vfs_fstype {
    const char *name;
    struct vfs_ops *ops;
    int (*mount)(const char *dev, const char *dir);
    struct vfs_fstype *next;
};

/* Mount table entry */
struct vfs_mount {
    char path[VFS_PATH_MAX];
    char dev[64];
    struct vfs_fstype *fs;
    struct vfs_mount *next;
};

/* Open file handle — created by vfs_open, freed by vfs_close */
struct vfs_file {
    char path[VFS_PATH_MAX];
    off_t pos;
    int flags;
};

/* Directory entry returned by vfs_readdir */
struct vfs_dirent {
    char name[VFS_PATH_MAX];
    uint8_t type;
};

/*
 * d_type values for vfs_dirent (Linux getdents subset, stable ABI).
 */
#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8

/* Lifecycle */
int vfs_init(void);
int vfs_register_fs(struct vfs_fstype *fs);
int vfs_mount(const char *dev, const char *path, const char *fstype);
int vfs_init_with_minix(void);
int vfs_init_root(void);

/* Mount table (single linked list, newest first); NULL if empty */
struct vfs_mount *vfs_get_mounts(void);

/* File operations */
int vfs_open(const char *path, int flags, mode_t mode, struct vfs_file **out);
int vfs_read(struct vfs_file *f, char *buf, size_t count);
int vfs_write(struct vfs_file *f, const char *buf, size_t count);
int vfs_close(struct vfs_file *f);
off_t vfs_lseek(struct vfs_file *f, off_t offset, int whence);

/* Path operations */
int vfs_stat(const char *path, stat_t *buf);
int vfs_mkdir(const char *path, int mode);
int vfs_unlink(const char *path);
int vfs_rmdir(const char *path);
int vfs_rmdir_recursive(const char *path);
int vfs_link(const char *oldpath, const char *newpath);
int vfs_rename(const char *oldpath, const char *newpath);
int vfs_readdir(const char *path, struct vfs_dirent *entries, int max);
int vfs_chown(const char *path, uid_t owner, gid_t group);
int vfs_chmod(const char *path, mode_t mode);
int vfs_truncate(const char *path, size_t length);

/* Bulk read helper (ELF loader, etc.) */
int vfs_read_file(const char *path, void **data, size_t *size);
