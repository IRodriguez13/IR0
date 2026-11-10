/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vfs_internal.h
 * Description: Internal facade shared by the fs/vfs*.c units (core, mount
 *              table, open files, path operations). Not a public API: other
 *              subsystems use fs/vfs.h and the ir0/ facades.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include "vfs.h"

#define MAX_PATH 256

/* One fs_ops critical section at a time; recursive for the owner (vfs.c). */
void vfs_fs_op_acquire(void);
void vfs_fs_op_release(void);

/* Path of @path inside the filesystem that owns it (vfs.c). */
int vfs_backend_path(struct vfs_ops *ops, const char *path,
		     char *backend, size_t backend_size);

/* Backend calls under the fs-op lock, on the backend path (vfs.c). */
int vfs_ops_stat(struct vfs_ops *ops, const char *path, stat_t *st);
int vfs_ops_read(struct vfs_ops *ops, const char *path, void *buf,
		 size_t count, size_t *done, off_t offset);
int vfs_ops_write(struct vfs_ops *ops, const char *path, const void *buf,
		  size_t count, size_t *done, off_t offset);
int vfs_ops_create(struct vfs_ops *ops, const char *path, mode_t mode);
int vfs_ops_mkdir(struct vfs_ops *ops, const char *path, mode_t mode);
int vfs_ops_unlink(struct vfs_ops *ops, const char *path);
int vfs_ops_rmdir(struct vfs_ops *ops, const char *path);
int vfs_ops_readdir(struct vfs_ops *ops, const char *path,
		    struct vfs_dirent *entries, int max);
int vfs_ops_link(struct vfs_ops *ops, const char *oldpath,
		 const char *newpath);
int vfs_ops_symlink(struct vfs_ops *ops, const char *target,
		    const char *linkpath);
int vfs_ops_readlink(struct vfs_ops *ops, const char *path, char *buf,
		     size_t buflen);
int vfs_ops_rename(struct vfs_ops *ops, const char *oldpath,
		   const char *newpath);
int vfs_ops_chown(struct vfs_ops *ops, const char *path, uid_t owner,
		  gid_t group);
int vfs_ops_chmod(struct vfs_ops *ops, const char *path, mode_t mode);
int vfs_ops_truncate(struct vfs_ops *ops, const char *path, size_t length);
int vfs_ops_utimens(struct vfs_ops *ops, const char *path,
		    const struct timespec times[2]);

/* Path helpers (vfs.c). */
int vfs_validate_path(const char *path);
struct vfs_ops *vfs_ops_for_path(const char *path);
int vfs_check_dir_traverse(const char *path);
int vfs_parent_dir(const char *path, char *out, size_t out_sz);
int vfs_lookup_parent(const char *path, char *out, size_t out_sz);

/* Mount table lookups (vfs_mount.c). */
struct vfs_mount *vfs_find_mount(const char *path);
int vfs_mount_is_readonly(const char *path);
