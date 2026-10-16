/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: vfs_file.h
 * Description: Open-file description (Linux struct file / fd_install).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/types.h>
#include <stddef.h>

#define IR0_VFS_PATH_MAX 256

struct fd_resource_ops;

/*
 * Linux fd_install() stores a struct file *, not a path string.
 * IR0's open-file description is this object: path, cursor, flags, ref.
 * Path I/O goes through vfs_dispatch / vfs_ops; this handle is generic-core.
 * https://www.kernel.org/doc/html/latest/filesystems/porting.html
 */
struct vfs_file
{
	char path[IR0_VFS_PATH_MAX];
	off_t pos;
	int flags;
	int ref_count;
};

struct vfs_file *vfs_file_alloc(const char *path, int flags);
void vfs_file_acquire(struct vfs_file *f);
int vfs_file_put(struct vfs_file *f);
int vfs_file_put_last(struct vfs_file *f, void (*on_last)(struct vfs_file *f));

struct ir0_vfs_file_ops
{
	struct vfs_file *(*alloc)(const char *path, int flags);
	void (*acquire)(struct vfs_file *f);
	int (*put)(struct vfs_file *f);
};

const struct ir0_vfs_file_ops *ir0_vfs_file_product(void);
const struct fd_resource_ops *ir0_vfs_file_resource_ops(void);
