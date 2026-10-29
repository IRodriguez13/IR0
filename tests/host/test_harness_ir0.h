/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_harness_ir0.h
 * Description: Host harness macros without stdlib.h (avoids dev_t vs IR0 types conflicts).
 */

#ifndef IR0_TEST_HARNESS_IR0_H
#define IR0_TEST_HARNESS_IR0_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern int _ir0_test_failed;
extern int _ir0_test_count;
extern int _ir0_test_pass;

#define TEST_BEGIN(name) do { \
	_ir0_test_failed = 0; \
	_ir0_test_count++; \
	fprintf(stderr, "[TEST] %s ... ", (name)); \
	fflush(stderr); \
} while (0)

#define TEST_END() do { \
	if (_ir0_test_failed) { \
		fprintf(stderr, "FAIL\n"); \
		_ir0_test_pass = 0; \
	} else { \
		fprintf(stderr, "PASS\n"); \
	} \
} while (0)

#define ASSERT(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "\n[TEST] ASSERT failed: %s (%s:%d)\n", \
			#cond, __FILE__, __LINE__); \
		_ir0_test_failed = 1; \
	} \
} while (0)

/*
 * Single evaluation and no long cast: see test_harness.h.  A host test that
 * passes a stateful helper used to call it three times, and 64-bit values
 * were truncated in the failure report on ILP32.
 */
#define ASSERT_EQ(a, b) do { \
	__typeof__(a) _ir0_eq_a = (a); \
	__typeof__(b) _ir0_eq_b = (b); \
	if (!(_ir0_eq_a == _ir0_eq_b)) { \
		fprintf(stderr, \
			"\n[TEST] ASSERT_EQ failed: %s == %s (%s:%d) " \
			"(0x%llx != 0x%llx)\n", \
			#a, #b, __FILE__, __LINE__, \
			(unsigned long long)(uintptr_t)_ir0_eq_a, \
			(unsigned long long)(uintptr_t)_ir0_eq_b); \
		_ir0_test_failed = 1; \
	} \
} while (0)

#define ASSERT_NE(a, b) do { \
	__typeof__(a) _ir0_ne_a = (a); \
	__typeof__(b) _ir0_ne_b = (b); \
	if (_ir0_ne_a == _ir0_ne_b) { \
		fprintf(stderr, \
			"\n[TEST] ASSERT_NE failed: %s != %s (%s:%d) " \
			"(both 0x%llx)\n", \
			#a, #b, __FILE__, __LINE__, \
			(unsigned long long)(uintptr_t)_ir0_ne_a); \
		_ir0_test_failed = 1; \
	} \
} while (0)

#define ASSERT_STR_EQ(a, b) do { \
	const char *_ir0_str_a = (a); \
	const char *_ir0_str_b = (b); \
	if (!_ir0_str_a || !_ir0_str_b || \
	    strcmp(_ir0_str_a, _ir0_str_b) != 0) { \
		fprintf(stderr, \
			"\n[TEST] ASSERT_STR_EQ failed: \"%s\" != \"%s\" " \
			"(%s:%d)\n", \
			_ir0_str_a ? _ir0_str_a : "(null)", \
			_ir0_str_b ? _ir0_str_b : "(null)", \
			__FILE__, __LINE__); \
		_ir0_test_failed = 1; \
	} \
} while (0)

#endif
