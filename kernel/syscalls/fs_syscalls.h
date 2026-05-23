/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel — file-related syscall implementations (split from syscalls.c).
 * Public prototypes remain in kernel/syscalls.h; dispatch stays centralized.
 */

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
