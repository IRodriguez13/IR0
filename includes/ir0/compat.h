/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: compat.h
 * Description: IR0 kernel source/header file
 */

#include <ir0/oops.h>
#include <stddef.h>


#ifdef __cplusplus
// Global new/delete operators
void* operator_new(size_t size);
void operator_delete(void* ptr) noexcept;
void operator_delete(void* ptr, size_t size) noexcept;


// Array new/delete operators
void* operator_new_array(size_t size);
void operator_delete_array(void* ptr) noexcept;
void operator_delete_array(void* ptr, size_t size) noexcept;

// C++ runtime support functions
extern "C" void __cxa_pure_virtual(void);
extern "C" int __cxa_guard_acquire(unsigned long long *guard);
extern "C" void __cxa_guard_release(unsigned long long *guard);
extern "C" void __cxa_guard_abort(unsigned long long *guard);

#else
// C linkage declarations (no operators)
#endif
