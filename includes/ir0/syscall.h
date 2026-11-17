/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscall.h
 * Description: IR0 kernel source/header file
 */

#pragma once

#ifndef _IR0_SYSCALL_H
#define _IR0_SYSCALL_H

#include <stdint.h>
#include <stddef.h>
#include <ir0/stat.h>
#include <ir0/types.h>
#include <ir0/fcntl.h>
#include <ir0/poll.h>
#include <ir0/time.h>
#include <ir0/mman.h>
#include <string.h>

typedef int syscall_num_t;

/* Virtual filesystem paths for file operations */
#define VFS_PROC_PATH     "/proc"
#define VFS_DEV_PATH       "/dev"
#define VFS_SYS_PATH       "/sys"

/* Standard device nodes */
#define DEV_NULL           "/dev/null"
#define DEV_ZERO           "/dev/zero"
#define DEV_CONSOLE        "/dev/console"
#define DEV_TTY            "/dev/tty"
#define DEV_AUDIO          "/dev/audio"
#define DEV_MOUSE          "/dev/mouse"
#define DEV_NET            "/dev/net"
#define DEV_DISK           "/dev/disk"
#define DEV_KMSG           "/dev/kmsg"
#define DEV_FB0            "/dev/fb0"
#define DEV_EVENTS0        "/dev/events0"

/* Process information files in /proc */
#define PROC_STATUS        "/proc/self/status"
#define PROC_MEMINFO       "/proc/meminfo"
#define PROC_CPUINFO       "/proc/cpuinfo"
#define PROC_MOUNTS        "/proc/mounts"
#define PROC_VERSION       "/proc/version"

/* System information files in /proc (legacy /sys paths redirected) */
#define SYS_NETINFO_PATH   "/proc/netinfo"
#define SYS_DRIVERS_PATH   "/proc/drivers"
#define SYS_UPTIME_PATH    "/proc/uptime"

/* Standard file descriptors */
#define STDIN_FILENO   0
#define STDOUT_FILENO  1
#define STDERR_FILENO  2

/* Access modes for permission checking - use definitions from cred.h */
#include <ir0/cred.h>
#define ACCESS_EXECUTE ACCESS_EXEC

#endif /* _IR0_SYSCALL_H */
