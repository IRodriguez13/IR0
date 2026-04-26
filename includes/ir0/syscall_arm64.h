/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscall_arm64.h
 * Description: ARM64 userspace syscall ABI wrappers (Linux-like calling convention).
 */

#pragma once

#include <stdint.h>

static inline int64_t syscall0(int64_t num)
{
    register int64_t x8 __asm__("x8") = num;
    register int64_t x0 __asm__("x0");
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    return x0;
}

static inline int64_t syscall1(int64_t num, int64_t arg1)
{
    register int64_t x8 __asm__("x8") = num;
    register int64_t x0 __asm__("x0") = arg1;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return x0;
}

static inline int64_t syscall2(int64_t num, int64_t arg1, int64_t arg2)
{
    register int64_t x8 __asm__("x8") = num;
    register int64_t x0 __asm__("x0") = arg1;
    register int64_t x1 __asm__("x1") = arg2;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1) : "memory");
    return x0;
}

static inline int64_t syscall3(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3)
{
    register int64_t x8 __asm__("x8") = num;
    register int64_t x0 __asm__("x0") = arg1;
    register int64_t x1 __asm__("x1") = arg2;
    register int64_t x2 __asm__("x2") = arg3;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}

static inline int64_t syscall6(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4, int64_t arg5, int64_t arg6)
{
    register int64_t x8 __asm__("x8") = num;
    register int64_t x0 __asm__("x0") = arg1;
    register int64_t x1 __asm__("x1") = arg2;
    register int64_t x2 __asm__("x2") = arg3;
    register int64_t x3 __asm__("x3") = arg4;
    register int64_t x4 __asm__("x4") = arg5;
    register int64_t x5 __asm__("x5") = arg6;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
    return x0;
}
