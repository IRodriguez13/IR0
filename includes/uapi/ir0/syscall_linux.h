/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscall_linux.h
 * Description: Public UAPI — IR0 syscall ABI on x86-64: the Linux numbers of
 * <asm/unistd.h> (what the decoder dispatches) plus the IR0-specific range.
 * Exported by `make headers_install`.
 */

#ifndef _IR0_UAPI_SYSCALL_LINUX_H
#define _IR0_UAPI_SYSCALL_LINUX_H

#include <asm/unistd.h>

/* Linux x86-64 number not dispatched yet; the KTM probe still names it. */
#define __NR_mincore       27

/*
 * Linux time64 syscalls (musl / BusyBox may call these even on x86_64 when built
 * against recent kernel headers). Must not collide with IR0 custom numbers.
 */
#define __NR_clock_gettime64 403
#define __NR_clock_settime64 404
#define __NR_clock_adjtime64 405
#define __NR_clock_getres_time64 406
#define __NR_clock_nanosleep_time64 407

/* IR0 custom syscalls (440+ — outside Linux x86-64 assigned range) */
#define __NR_console_scroll   440
#define __NR_console_clear    441
#define __NR_keymap_set       442
#define __NR_keymap_get       443
/* 444 was sudo_auth: privilege elevation is userspace policy (doas/sudo). */

/* Max syscall number we handle (for table size) */
#define __NR_syscall_max   450

#endif /* _IR0_UAPI_SYSCALL_LINUX_H */
