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
 * Description: Public UAPI — the IR0 syscall ABI of the selected ISA: the
 * Linux numbers of <asm/unistd.h> plus the IR0-specific ones of
 * <asm/unistd_ir0.h>.  Both live in arch/<isa>/include/uapi/asm/; this name
 * stays stable for userspace (ISD lib/ir0_keymap.c).
 * Exported by `make headers_install`.
 */

#ifndef _IR0_UAPI_SYSCALL_LINUX_H
#define _IR0_UAPI_SYSCALL_LINUX_H

#include <asm/unistd.h>
#include <asm/unistd_ir0.h>

#endif /* _IR0_UAPI_SYSCALL_LINUX_H */
