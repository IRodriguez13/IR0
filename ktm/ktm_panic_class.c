/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_panic_class.c
 * Description: IR0 kernel source — ktm panic class
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm.h>
#include <ir0/process.h>
#include <ir0/ktm/klog.h>
#include <string.h>


#define USER_RIP_LO    0x00400000ULL
#define USER_RIP_HI    0x00007FFFFFFFFFFFULL

/* Mirrors panic_level_t in includes/ir0/oops.h (no include — avoid coupling). */
#define KTM_PL_KERNEL_BUG       0
#define KTM_PL_HARDWARE_FAULT   1
#define KTM_PL_OUT_OF_MEMORY    2
#define KTM_PL_STACK_OVERFLOW   3
#define KTM_PL_ASSERT_FAILED    4
#define KTM_PL_MEM              5

