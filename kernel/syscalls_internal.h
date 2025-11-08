/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - System Call Implementations
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Internal syscall implementation functions
 * These are called by the syscall dispatcher
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <ir0/stat.h>

/* Type definitions */
typedef uint32_t mode_t;
typedef long off_t;

/* ========================================================================== */
/* PROCESS MANAGEMENT                                                         */
/* ========================================================================== */

int64_t sys_exit(int exit_code);
int64_t sys_fork(void);
int64_t sys_wait4(int pid, int *status, int options, void *rusage);
int64_t sys_getpid(void);
int64_t sys_getppid(void);

/* ========================================================================== */
/* FILE OPERATIONS                                                            */
/* ========================================================================== */

int64_t sys_read(int fd, void *buf, size_t count);
int64_t sys_write(int fd, const void *buf, size_t count);
int64_t sys_open(const char *pathname, int flags, mode_t mode);
int64_t sys_close(int fd);
int64_t sys_lseek(int fd, off_t offset, int whence);

/* ========================================================================== */
/* FILE SYSTEM                                                                */
/* ========================================================================== */

int64_t sys_stat(const char *pathname, stat_t *buf);
int64_t sys_fstat(int fd, stat_t *buf);
int64_t sys_mkdir(const char *pathname, mode_t mode);
int64_t sys_rmdir(const char *pathname);
int64_t sys_ls(const char *pathname);
int64_t sys_chdir(const char *pathname);
int64_t sys_getcwd(char *buf, size_t size);
int64_t sys_unlink(const char *pathname);

/* ========================================================================== */
/* MEMORY MANAGEMENT                                                          */
/* ========================================================================== */

int64_t sys_brk(void *addr);
int64_t sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int64_t sys_munmap(void *addr, size_t length);
