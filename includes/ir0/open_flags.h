/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: open_flags.h
 * Description: Linux open(2) ABI → IR0 internal open flags for VFS.
 *
 * Syscall entry (sys_open, sys_openat) must translate user flags with
 * linux_open_flags_to_ir0() before calling VFS or pseudo-fs open helpers.
 * VFS operates only on IR0_O_* values defined here.
 */

#ifndef _IR0_OPEN_FLAGS_H
#define _IR0_OPEN_FLAGS_H

#include <stdint.h>

/* File access modes (same numeric values as Linux/musl accmode) */
#define IR0_O_RDONLY    0x0000
#define IR0_O_WRONLY    0x0001
#define IR0_O_RDWR      0x0002
#define IR0_O_ACCMODE   0x0003

/* IR0 internal status flags (includes/ir0/fcntl.h O_* aliases these) */
#define IR0_O_APPEND    0x0008
#define IR0_O_NONBLOCK  0x0800
#define IR0_O_CREAT     0x0100
#define IR0_O_EXCL      0x0200
#define IR0_O_TRUNC     0x0400
#define IR0_O_DIRECTORY 0x0200000
#define IR0_O_CLOEXEC   0x80000

/*
 * Linux x86-64 open(2) status flag bits (musl/glibc uapi).
 * Must not appear in flags passed to vfs_open after translation.
 */
#define LINUX_O_CREAT     0x0040U
#define LINUX_O_EXCL      0x0080U
#define LINUX_O_NOCTTY    0x0100U
#define LINUX_O_TRUNC     0x0200U
#define LINUX_O_APPEND    0x0400U
#define LINUX_O_NONBLOCK  0x0800U
#define LINUX_O_LARGEFILE 0x8000U
#define LINUX_O_DIRECTORY 0x010000U
#define LINUX_O_NOFOLLOW  0x020000U
#define LINUX_O_CLOEXEC   0x080000U

/**
 * linux_open_flags_to_ir0 - Translate Linux/musl open flags to IR0 VFS flags.
 * @linux_flags: Raw flags from userspace (Linux ABI).
 * Returns: IR0_O_* bitmask suitable for vfs_open and fd_table->flags.
 */
int linux_open_flags_to_ir0(int linux_flags);

/**
 * ir0_open_flags_ok_for_vfs - Reject leaked Linux ABI status bits.
 * @flags: Flags after linux_open_flags_to_ir0().
 * Returns: 1 if safe for VFS, 0 if raw Linux bits leaked through.
 */
int ir0_open_flags_ok_for_vfs(int flags);

/**
 * ir0_open_flags_log_translation - Serial trace + OPEN_ABI_TRANSLATION_LAYER_OK.
 */
void ir0_open_flags_log_translation(int linux_raw, int ir0_flags);

#endif /* _IR0_OPEN_FLAGS_H */
