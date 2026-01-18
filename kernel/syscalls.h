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
int64_t sys_kill(pid_t pid, int signal);

/* File operations */
int64_t sys_read(int fd, void *buf, size_t count);
int64_t sys_write(int fd, const void *buf, size_t count);
int64_t sys_open(const char *pathname, int flags, mode_t mode);
int64_t sys_close(int fd);
int64_t sys_lseek(int fd, off_t offset, int whence);
int64_t sys_dup2(int oldfd, int newfd);
int64_t sys_creat(const char *pathname, mode_t mode);

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
int64_t sys_mount(const char *dev, const char *mountpoint, const char *fstype);

/* Memory management */
int64_t sys_brk(void *addr);
void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int sys_munmap(void *addr, size_t length);
int sys_mprotect(void *addr, size_t len, int prot);

/* System functions */
void syscalls_init(void);
int64_t syscall_dispatch(uint64_t syscall_num, uint64_t arg1, uint64_t arg2,
			 uint64_t arg3, uint64_t arg4, uint64_t arg5);
