/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fcntl.h
 * Description: IR0 kernel source/header file
 */

#ifndef _IR0_FCNTL_H
#define _IR0_FCNTL_H

/*
 * Kernel-internal open flags (IR0_O_*).  Syscall entry translates Linux ABI
 * via linux_open_flags_to_ir0() in includes/ir0/open_flags.h before VFS.
 */
#include <ir0/open_flags.h>

#define O_RDONLY    IR0_O_RDONLY
#define O_WRONLY    IR0_O_WRONLY
#define O_RDWR      IR0_O_RDWR
#define O_ACCMODE   IR0_O_ACCMODE
#define O_APPEND    IR0_O_APPEND
#define O_NONBLOCK  IR0_O_NONBLOCK
#define O_CREAT     IR0_O_CREAT
#define O_EXCL      IR0_O_EXCL
#define O_TRUNC     IR0_O_TRUNC
#define O_DIRECTORY IR0_O_DIRECTORY
#define O_CLOEXEC   IR0_O_CLOEXEC

/* fcntl commands (OSDev/Linux) */
#define F_DUPFD         0   /* Duplicate fd to lowest >= arg */
#define F_GETFD         1   /* Get fd flags (FD_CLOEXEC) */
#define F_SETFD         2   /* Set fd flags */
#define F_GETFL         3   /* Get file status flags */
#define F_SETFL         4   /* Set file status flags */
#define F_GETOWN        5   /* Get owner for SIGIO (Linux x86-64) */
#define F_SETOWN        8   /* Set owner for SIGIO (Linux x86-64) */

#define FD_CLOEXEC      1   /* Close-on-exec flag */

/* Linux open status bit; IR0 does not deliver SIGIO yet — strip on F_SETFL. */
#ifndef O_ASYNC
#define O_ASYNC         020000
#endif
#define FASYNC          O_ASYNC

/* flock(2) operations (Linux uapi) */
#define LOCK_SH  1
#define LOCK_EX  2
#define LOCK_UN  8
#define LOCK_NB  4

/* Seek types */
#define SEEK_SET    0       /* Set file offset to offset */
#define SEEK_CUR    1       /* Set file offset to current plus offset */
#define SEEK_END    2       /* Set file offset to EOF plus offset */

#endif /* _IR0_FCNTL_H */