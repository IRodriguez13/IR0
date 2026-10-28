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
 *
 * Diagnostics use INFO, not DEBUG: the canonical test ISO boots with
 * ir0.loglevel=normal, so DEBUG records never reach /tmp/ktest.log and a
 * failing assertion would be indistinguishable from a silent "not ok".
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
extern const char *_ktest_current_name;

#define KTEST_BEGIN(name) do { \
	_ktest_failed = 0; \
	_ktest_count++; \
	_ktest_current_name = (name); \
} while (0)

#define KTEST_END() do { \
	klog_info_fmt("KTEST", "[KTEST] %s ... %s", \
		      _ktest_current_name ? _ktest_current_name : "unknown", \
		      _ktest_failed ? "FAIL" : "PASS"); \
	if (_ktest_failed) \
		_ktest_pass = 0; \
} while (0)

#define KASSERT(cond) do { \
	if (!(cond)) { \
		klog_info_fmt("KTEST", "[KTEST] ASSERT failed: %s (line %u)", \
			      #cond, (unsigned)__LINE__); \
		_ktest_failed = 1; \
	} \
} while (0)

#define KASSERT_EQ(a, b) do { \
	__typeof__(a) _ktest_a = (a); \
	__typeof__(b) _ktest_b = (b); \
	if (_ktest_a != _ktest_b) { \
		klog_info_fmt("KTEST", \
			      "[KTEST] ASSERT_EQ failed: %s != %s (line %u) " \
			      "left=0x%llx right=0x%llx", \
			      #a, #b, (unsigned)__LINE__, \
			      (unsigned long long)_ktest_a, \
			      (unsigned long long)_ktest_b); \
		_ktest_failed = 1; \
	} \
} while (0)

#define KASSERT_GE(a, b) do { \
	__typeof__(a) _ktest_a = (a); \
	__typeof__(b) _ktest_b = (b); \
	if (_ktest_a < _ktest_b) { \
		klog_info_fmt("KTEST", \
			      "[KTEST] ASSERT_GE failed: %s >= %s (line %u) " \
			      "left=0x%llx right=0x%llx", \
			      #a, #b, (unsigned)__LINE__, \
			      (unsigned long long)_ktest_a, \
			      (unsigned long long)_ktest_b); \
		_ktest_failed = 1; \
	} \
} while (0)

#define KASSERT_GT(a, b) do { \
	__typeof__(a) _ktest_a = (a); \
	__typeof__(b) _ktest_b = (b); \
	if (_ktest_a <= _ktest_b) { \
		klog_info_fmt("KTEST", \
			      "[KTEST] ASSERT_GT failed: %s > %s (line %u) " \
			      "left=0x%llx right=0x%llx", \
			      #a, #b, (unsigned)__LINE__, \
			      (unsigned long long)_ktest_a, \
			      (unsigned long long)_ktest_b); \
		_ktest_failed = 1; \
	} \
} while (0)

int kernel_test_run_all(void);

#endif
