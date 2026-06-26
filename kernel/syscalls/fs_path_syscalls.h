/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fs_path_syscalls.h
 * Description: path-based filesystem syscalls (split from syscalls.c)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/types.h>
#include <ir0/utsname.h>
#include <ir0/time.h>

int64_t sys_mount(const char *dev, const char *mountpoint, const char *fstype);
int64_t sys_umount(const char *target, int flags);
int64_t sys_mkdir(const char *pathname, mode_t mode);
int64_t sys_chmod(const char *path, mode_t mode);
int64_t sys_chown(const char *path, uid_t owner, gid_t group);
int64_t sys_link(const char *oldpath, const char *newpath);
int64_t sys_rename(const char *oldpath, const char *newpath);
int64_t sys_uname(struct utsname *buf);
int64_t sys_access(const char *pathname, int mode);
int64_t sys_faccessat(int dirfd, const char *pathname, int mode, int flags);
int64_t sys_chdir(const char *pathname);
int64_t sys_getcwd(char *buf, size_t size);
int64_t sys_utimensat(int dirfd, const char *pathname,
                      const struct timespec *times, int flags);
int64_t sys_unlink(const char *pathname);
int64_t sys_truncate(const char *pathname, off_t length);
int64_t sys_rmdir(const char *pathname);
