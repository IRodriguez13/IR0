/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: bpath.h
 * Description: Boot-path VFS ops for staged erfs routing onto product mounts.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <ir0/stat.h>
#include <ir0/vfs_file.h>

#define IR0_BPATH_TMP_PREF "/tmp/"
#define IR0_BPATH_RUN_PREF "/run/"

struct ir0_bpath_dirent
{
	char name[40];
	uint8_t type;
};

struct ir0_bpath_ops
{
	int (*stat)(const char *path, stat_t *st);
	int (*open)(const char *path, int flags, mode_t mode,
		    struct vfs_file **out);
	int (*read)(struct vfs_file *f, void *buf, size_t n);
	int (*write)(struct vfs_file *f, const void *buf, size_t n);
	off_t (*lseek)(struct vfs_file *f, off_t off, int whence);
	int (*put)(struct vfs_file *f);
	int (*mkdir)(const char *path, int mode);
	int (*unlink)(const char *path);
	int (*rmdir)(const char *path);
	int (*truncate)(const char *path, size_t len);
	int (*readdir)(const char *path, struct ir0_bpath_dirent *ents,
		       int max);
};

void early_rootfs_bind_bpath(const struct ir0_bpath_ops *ops);
int early_rootfs_bpath_used(void);
int ir0_bpath_is_reserved_early(const char *path);
int ir0_bpath_is_foldable(const char *path);
int ir0_bpath_is_tmp(const char *path);
const struct ir0_bpath_ops *ir0_bpath_bound(void);

/* Non-zero when product VFS serves staged bpath prefixes (erfs fold path). */
int bpath_live(void);
