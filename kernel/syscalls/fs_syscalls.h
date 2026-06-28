/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fs_syscalls.h
 * Description: IR0 kernel header — fs syscalls
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/types.h>
#include <stddef.h>
#include <ir0/uio.h>

struct stat;
typedef struct stat stat_t;

int64_t sys_read(int fd, void *buf, size_t count);
int64_t sys_write(int fd, const void *buf, size_t count);
int64_t sys_readv(int fd, const struct iovec *iov, int iovcnt);
int64_t sys_writev(int fd, const struct iovec *iov, int iovcnt);
int64_t sys_open(const char *pathname, int flags, mode_t mode);
int64_t sys_openat(int dirfd, const char *pathname, int flags, mode_t mode);
int64_t sys_stat(const char *pathname, stat_t *buf);
int64_t sys_fstat(int fd, stat_t *buf);
int64_t sys_newfstatat(int dirfd, const char *pathname, stat_t *buf, int flags);
int64_t sys_unlinkat(int dirfd, const char *pathname, int flags);
int64_t sys_renameat(int olddirfd, const char *oldpath,
                     int newdirfd, const char *newpath);
int64_t sys_readlink(const char *pathname, char *buf, size_t bufsiz);
int64_t sys_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz);
int64_t sys_symlink(const char *target, const char *linkpath);
int64_t sys_symlinkat(const char *target, int dirfd, const char *linkpath);
int64_t sys_fchmod(int fd, mode_t mode);
int64_t sys_fchown(int fd, uid_t owner, gid_t group);
int64_t sys_fchmodat(int dirfd, const char *pathname, mode_t mode, int flags);
int64_t sys_fchownat(int dirfd, const char *pathname, uid_t owner, gid_t group,
		     int flags);
int64_t sys_mknod(const char *pathname, unsigned int mode, unsigned int dev);
int64_t sys_mknodat(int dirfd, const char *pathname, unsigned int mode,
		    unsigned int dev);
int64_t sys_flock(int fd, int operation);
int64_t sys_fchdir(int fd);
int64_t sys_getdents(int fd, void *dirent, size_t count);
int64_t sys_getdents64(int fd, void *dirent, size_t count);
int64_t sys_setsid(void);
