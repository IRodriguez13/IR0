/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscall_stub.c
 * Description: ARM64 syscall entry scaffold.
 */

#include <stdint.h>

int64_t syscall_entry_arm64(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3,
                            uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    (void)num;
    (void)arg1;
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg5;
    (void)arg6;
    return -1;
}
