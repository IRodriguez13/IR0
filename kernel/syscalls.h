/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscalls.h
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - System Calls
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Public interface for system call subsystem
 */

#pragma once

#include <ir0/types.h>
#include <stddef.h>
#include <kernel/process.h>
#include <ir0/poll.h>
#include <ir0/time.h>

/* Forward declarations */
struct stat;
typedef struct stat stat_t;


/**
 * POSIX-compliant System Call Implementations
 * 
 * These are the actual syscall handler functions called by the dispatcher.
 * They should NOT be called directly from user code - use the POSIX wrapper
 * functions in <ir0/syscall.h> instead (ir0_open, ir0_read, etc.).
 */

/* Process management */
int64_t sys_exit(int exit_code);
int64_t sys_fork(void);
int64_t sys_exec(const char *pathname, char *const argv[], char *const envp[]);
int64_t sys_waitpid(pid_t pid, int *status, int options);
int64_t sys_getpid(void);
int64_t sys_getppid(void);
int64_t sys_getuid(void);
int64_t sys_geteuid(void);
int64_t sys_getgid(void);
int64_t sys_getegid(void);
int64_t sys_setuid(uid_t uid);
int64_t sys_setgid(gid_t gid);
int64_t sys_umask(mode_t mask);
int64_t sys_sudo_auth(const char *password);
int64_t sys_kill(pid_t pid, int signal);

/* File operations */
int64_t sys_read(int fd, void *buf, size_t count);
int64_t sys_write(int fd, const void *buf, size_t count);
int64_t sys_open(const char *pathname, int flags, mode_t mode);
int64_t sys_close(int fd);
int64_t sys_lseek(int fd, off_t offset, int whence);
int64_t sys_dup2(int oldfd, int newfd);
int64_t sys_ioctl(int fd, uint64_t request, void *arg);

/* File system operations */
int64_t sys_stat(const char *pathname, stat_t *buf);
int64_t sys_fstat(int fd, stat_t *buf);
int64_t sys_mkdir(const char *pathname, mode_t mode);
int64_t sys_rmdir(const char *pathname);
int64_t sys_chdir(const char *pathname);
int64_t sys_getcwd(char *buf, size_t size);
int64_t sys_unlink(const char *pathname);
int64_t sys_link(const char *oldpath, const char *newpath);
int64_t sys_chmod(const char *path, mode_t mode);
int64_t sys_chown(const char *path, uid_t owner, gid_t group);
int64_t sys_mount(const char *dev, const char *mountpoint, const char *fstype);
int64_t sys_getdents(int fd, void *dirent, size_t count);
int64_t sys_poll(struct pollfd *fds, unsigned int nfds, int timeout_ms);
int64_t sys_pipe(int pipefd[2]);
int64_t sys_nanosleep(const struct timespec *req, struct timespec *rem);
int64_t sys_gettimeofday(struct timeval *tv, void *tz);

/* Memory management */
int64_t sys_brk(void *addr);
void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int sys_munmap(void *addr, size_t length);
int sys_mprotect(void *addr, size_t len, int prot);

/* System functions */
void syscalls_init(void);
int64_t syscall_dispatch(uint64_t syscall_num, uint64_t arg1, uint64_t arg2,
			 uint64_t arg3, uint64_t arg4, uint64_t arg5,
			 uint64_t arg6);

/* Poll: desbloquea procesos que esperan en poll() cuando hay datos o timeout */
void poll_wake_check(void);
/* nanosleep: desbloquea procesos cuyo tiempo de sueño ha expirado */
void sleep_wake_check(void);
/* read(0): desbloquea procesos esperando teclado cuando hay datos */
void stdin_wake_check(void);
