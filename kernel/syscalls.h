/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscalls.h
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/types.h>
#include <stddef.h>
#include <ir0/uio.h>
#include <ir0/time.h>
#include <ir0/process.h>
#include <ir0/poll.h>
#include <ir0/time.h>
#include <ir0/utsname.h>
#include <ir0/signals.h>
#include <ir0/clock_wait.h>
#include "syscalls/process_syscalls.h"
#include "syscalls/mm_syscalls.h"
#include "syscalls/io_syscalls.h"

/* Forward declarations */
struct stat;
typedef struct stat stat_t;
struct pipe;
typedef struct pipe pipe_t;


/**
 * POSIX-compliant System Call Implementations
 * 
 * These are the actual syscall handler functions called by the dispatcher.
 * They should NOT be called directly from user code - use the POSIX wrapper
 * functions in <ir0/syscall.h> instead (ir0_open, ir0_read, etc.).
 */

/* Process management — see process_syscalls.h */
int64_t sys_uname(struct utsname *buf);
int64_t sys_access(const char *pathname, int mode);
int64_t sys_rename(const char *oldpath, const char *newpath);

/* File operations */
int64_t sys_read(int fd, void *buf, size_t count);
int64_t sys_write(int fd, const void *buf, size_t count);
int64_t sys_readv(int fd, const struct iovec *iov, int iovcnt);
int64_t sys_writev(int fd, const struct iovec *iov, int iovcnt);
int64_t sys_open(const char *pathname, int flags, mode_t mode);

/* File system operations */
int64_t sys_stat(const char *pathname, stat_t *buf);
int64_t sys_fstat(int fd, stat_t *buf);
int64_t sys_mkdir(const char *pathname, mode_t mode);
int64_t sys_rmdir(const char *pathname);
int64_t sys_chdir(const char *pathname);
int64_t sys_getcwd(char *buf, size_t size);
int64_t sys_utimensat(int dirfd, const char *pathname,
                      const struct timespec *times, int flags);
int64_t sys_unlink(const char *pathname);
int64_t sys_truncate(const char *pathname, off_t length);
int64_t sys_ftruncate(int fd, off_t length);
int64_t sys_unlinkat(int dirfd, const char *pathname, int flags);
int64_t sys_renameat(int olddirfd, const char *oldpath,
                     int newdirfd, const char *newpath);
int64_t sys_link(const char *oldpath, const char *newpath);
int64_t sys_chmod(const char *path, mode_t mode);
int64_t sys_chown(const char *path, uid_t owner, gid_t group);
int64_t sys_mount(const char *dev, const char *mountpoint, const char *fstype);
int64_t sys_umount(const char *target, int flags);
int64_t sys_faccessat(int dirfd, const char *pathname, int mode, int flags);
int64_t sys_poll(struct pollfd *fds, unsigned int nfds, int timeout_ms);
int64_t sys_nanosleep(const struct timespec *req, struct timespec *rem);
int64_t sys_gettimeofday(struct timeval *tv, void *tz);

/* Memory management — see mm_syscalls.h */

int64_t sys_rt_sigprocmask(int how, const sigset_t *set, sigset_t *oldset, size_t sigsetsize);

int64_t sys_openat(int dirfd, const char *pathname, int flags, mode_t mode);
int64_t sys_newfstatat(int dirfd, const char *pathname, stat_t *buf, int flags);
int64_t sys_clock_gettime(int clock_id, struct timespec *tp);

/* System functions */
void syscalls_init(void);
int64_t syscall_dispatch(uint64_t syscall_num, uint64_t arg1, uint64_t arg2,
			 uint64_t arg3, uint64_t arg4, uint64_t arg5,
			 uint64_t arg6);

/* Poll: desbloquea procesos que esperan en poll() cuando hay datos o timeout */
void poll_wake_check(void);
/* read(0): desbloquea procesos esperando teclado cuando hay datos */
void stdin_wake_check(void);
