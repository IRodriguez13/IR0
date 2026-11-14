/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: stddef.h
 * Description: IR0 kernel source/header file
 */

#ifndef _STDDEF_H
#define _STDDEF_H

/* Standard definitions for IR0 Kernel - Freestanding implementation */

/* NULL pointer constant */
#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void*)0)
#endif
#endif

/* These fundamental types are properties of the compiler ABI, not an ISA. */
#if !defined(__SIZE_TYPE__) || !defined(__PTRDIFF_TYPE__)
#error "Compiler does not provide size or pointer-difference types"
#endif
typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;

/* wchar_t - wide character type */
#ifndef __cplusplus
typedef int wchar_t;
#endif

/* offsetof macro - offset of member in structure */
#define offsetof(type, member) __builtin_offsetof(type, member)

/* max_align_t - type with strictest alignment requirement */
typedef struct 
{
    long long __ll;
    long double __ld;
} max_align_t;

#endif /* _STDDEF_H */
