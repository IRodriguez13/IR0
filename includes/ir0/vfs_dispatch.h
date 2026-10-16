/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: vfs_dispatch.h
 * Description: Portable vfs_ops lookup (Linux never calls ->read on the slot).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/vfs_backend.h>
#include <stddef.h>

struct vfs_file;

/*
 * Linux fs/read_write.c vfs_read() goes through file->f_op, not a
 * parallel fd-slot table. IR0 keeps that boundary in the generic core:
 * a bound vfs_ops table owns path I/O; the fd only holds vfs_file.
 * https://www.kernel.org/doc/html/latest/filesystems/porting.html
 */
int vfs_dispatch_bind(struct vfs_ops *ops);
struct vfs_ops *vfs_dispatch_ops(const char *path);
int vfs_dispatch_stat(const char *path, stat_t *st);
int vfs_dispatch_read(struct vfs_file *f, void *buf, size_t count);
int vfs_dispatch_bound(void);
