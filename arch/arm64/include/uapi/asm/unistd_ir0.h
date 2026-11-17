/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: unistd_ir0.h
 * Description: Public UAPI — arm64 syscall numbers IR0 adds to the Linux
 * table of <asm/unistd.h>.  Reached through <ir0/syscall_linux.h>.
 * Exported by `make headers_install`.
 *
 * The arm64 decoder (arch/arm64/sources/syscall_decode.c) dispatches only
 * Linux numbers, so this ISA defines no IR0-specific one: a program that
 * tests `#ifdef __NR_keymap_set` sees that the call does not exist here.
 */

#ifndef _ASM_ARM64_UNISTD_IR0_H
#define _ASM_ARM64_UNISTD_IR0_H

#endif /* _ASM_ARM64_UNISTD_IR0_H */
