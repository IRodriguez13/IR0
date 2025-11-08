/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - System Calls
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Public interface for system call subsystem
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/* Forward declarations */
struct stat;
typedef struct stat stat_t;
typedef uint32_t mode_t;
typedef long off_t;
typedef uint32_t pid_t;


#define SYS_READ       0
#define SYS_WRITE      1
#define SYS_OPEN       2
#define SYS_CLOSE      3
#define SYS_STAT       4
#define SYS_FSTAT      5
#define SYS_LSEEK      8
#define SYS_WRITE_FILE 8
#define SYS_MMAP       9
#define SYS_MUNMAP     11
#define SYS_BRK        12
#define SYS_GETPID     39
#define SYS_FORK       57
#define SYS_EXEC       56
#define SYS_EXIT       60
#define SYS_WAIT4      61
#define SYS_GETCWD     79
#define SYS_CHDIR      80
#define SYS_MKDIR      83
#define SYS_RMDIR      84
#define SYS_UNLINK     87
#define SYS_RMDIR_R    88
#define SYS_GETPPID    110

/* Special/Debug */
#define SYS_LS         5
#define SYS_PS         7
#define SYS_CAT        9
#define SYS_READ_FILE  10
#define SYS_LS_DETAILED 61


/* Syscall implementations */
int64_t sys_read(int fd, void *buf, size_t count);
int64_t sys_write(int fd, const void *buf, size_t count);
int64_t sys_open(const char *pathname, int flags, mode_t mode);
int64_t sys_mkdir(const char *pathname, mode_t mode);
int64_t sys_mount(const char *dev, const char *mountpoint, const char *fstype);

/* System functions */
void syscalls_init(void);
int64_t syscall_dispatch(uint64_t syscall_num, uint64_t arg1, uint64_t arg2,
			 uint64_t arg3, uint64_t arg4, uint64_t arg5);
