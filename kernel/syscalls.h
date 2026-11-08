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
#include <ir0/utsname.h>
#include <ir0/signals.h>
#include <ir0/clock_wait.h>
#include "syscalls/process_syscalls.h"
#include "syscalls/mm_syscalls.h"
#include "syscalls/io_syscalls.h"
#include "syscalls/fs_path_syscalls.h"
#include "syscalls/fs_syscalls.h"
#include "syscalls/time_syscalls.h"
#include "syscalls/syscall_dispatch.h"

/* Forward declarations */
struct stat;
struct pipe;
typedef struct pipe pipe_t;

/**
 * POSIX-compliant System Call Implementations
 * 
 * These are the actual syscall handler functions called by the dispatcher.
 * They should NOT be called directly from user code - use the POSIX wrapper
 * functions in <ir0/syscall.h> instead (ir0_open, ir0_read, etc.).
 */

/* Memory management — see mm_syscalls.h */

/* System functions */
void syscalls_init(void);

/* Poll: desbloquea procesos que esperan en poll() cuando hay datos o timeout */
/* read(0): desbloquea procesos esperando teclado cuando hay datos */
void stdin_wake_check(void);
int stdin_wake_check_nosched(void);
