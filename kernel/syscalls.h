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


/* Syscall implementations */
int64_t sys_read(int fd, void *buf, size_t count);
int64_t sys_write(int fd, const void *buf, size_t count);
int64_t sys_open(const char *pathname, int flags, mode_t mode);
int64_t sys_close(int fd);
int64_t sys_lseek(int fd, off_t offset, int whence);
int64_t sys_dup2(int oldfd, int newfd);
int64_t sys_mkdir(const char *pathname, mode_t mode);
int64_t sys_mount(const char *dev, const char *mountpoint, const char *fstype);
int64_t sys_df(void);
int64_t sys_get_block_devices(int fd, void *buf, size_t count);

/* System functions */
void syscalls_init(void);
int64_t syscall_dispatch(uint64_t syscall_num, uint64_t arg1, uint64_t arg2,
			 uint64_t arg3, uint64_t arg4, uint64_t arg5);

#define SYS_GET_BLOCK_DEVICES 92
