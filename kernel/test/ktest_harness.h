/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktest_harness.h
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Test harness (in-kernel)
 * Batería exhaustiva: ASSERT y logging vía KTM klog para tests dentro del kernel.
 */

#ifndef _KERNEL_KTEST_HARNESS_H
#define _KERNEL_KTEST_HARNESS_H

#include <ir0/open_flags.h>
#include <ir0/ktm/klog.h>
#include <stdint.h>

/*
 * In-kernel ktest open flags — sys_open() translates Linux/musl ABI via
 * linux_open_flags_to_ir0(); do not pass IR0_O_* here.
 */
#define KTEST_O_RDONLY  0
#define KTEST_O_WRONLY  0x0001
#define KTEST_O_RDWR    0x0002
#define KTEST_O_CREAT   (int)LINUX_O_CREAT
#define KTEST_O_TRUNC   (int)LINUX_O_TRUNC
#define KTEST_O_EXCL    (int)LINUX_O_EXCL

extern int _ktest_failed;
extern int _ktest_count;
extern int _ktest_pass;

#define KTEST_BEGIN(name) do { \
	_ktest_failed = 0; \
	_ktest_count++; \
	klog_debug_fmt("KTEST", "[KTEST] %s ...", (name)); \
} while (0)

#define KTEST_END() do { \
	if (_ktest_failed) { \
		klog_debug("KTEST", "FAIL"); \
		_ktest_pass = 0; \
	} else { \
		klog_debug("KTEST", "PASS"); \
	} \
} while (0)

#define KASSERT(cond) do { \
	if (!(cond)) { \
		klog_debug_fmt("KTEST", "[KTEST] ASSERT failed: %s (line %x)", \
			       #cond, (unsigned)(uint32_t)__LINE__); \
		_ktest_failed = 1; \
	} \
} while (0)

#define KASSERT_EQ(a, b) do { \
	if ((a) != (b)) { \
		klog_debug_fmt("KTEST", "[KTEST] ASSERT_EQ failed: %s != %s (line %x)", \
			       #a, #b, (unsigned)(uint32_t)__LINE__); \
		_ktest_failed = 1; \
	} \
} while (0)

#define KASSERT_GE(a, b) do { \
	if ((a) < (b)) { \
		klog_debug("KTEST", "[KTEST] ASSERT_GE failed"); \
		_ktest_failed = 1; \
	} \
} while (0)

#define KASSERT_GT(a, b) do { \
	if ((a) <= (b)) { \
		klog_debug("KTEST", "[KTEST] ASSERT_GT failed"); \
		_ktest_failed = 1; \
	} \
} while (0)

void kernel_test_run_all(void);

#endif
