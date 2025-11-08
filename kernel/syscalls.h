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

/* ========================================================================== */
/* SYSCALL NUMBERS                                                            */
/* ========================================================================== */

#define SYS_READ       0
#define SYS_WRITE      1
#define SYS_OPEN       2
#define SYS_CLOSE      3
#define SYS_STAT       4
#define SYS_FSTAT      5
#define SYS_LSEEK      8
#define SYS_MMAP       9
#define SYS_MUNMAP     11
#define SYS_BRK        12
#define SYS_GETPID     39
#define SYS_FORK       57
#define SYS_EXIT       60
#define SYS_WAIT4      61
#define SYS_GETPPID    110
#define SYS_MKDIR      83
#define SYS_RMDIR      84

/* ========================================================================== */
/* TYPES                                                                      */
/* ========================================================================== */

typedef struct
{
	uint64_t arg1;
	uint64_t arg2;
	uint64_t arg3;
	uint64_t arg4;
	uint64_t arg5;
	uint64_t arg6;
} syscall_args_t;

/* ========================================================================== */
/* PUBLIC API                                                                 */
/* ========================================================================== */

void syscalls_init(void);
int64_t syscall_handler(uint64_t number, syscall_args_t *args);
