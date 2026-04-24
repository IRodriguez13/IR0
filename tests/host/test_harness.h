/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_harness.h
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Test harness
 * Macros y utilidades para batería de tests (unitarios e integración).
 * Solo se compila cuando se invoca make tests.
 */

#ifndef _IR0_TEST_HARNESS_H
#define _IR0_TEST_HARNESS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Estado global del harness (definido en test_runner.c) */
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

#define ASSERT_EQ(a, b) do { \
	if ((a) != (b)) { \
		fprintf(stderr, "\n[TEST] ASSERT_EQ failed: %s == %s (%s:%d) (%ld != %ld)\n", \
			#a, #b, __FILE__, __LINE__, (long)(a), (long)(b)); \
		_ir0_test_failed = 1; \
	} \
} while (0)

#define ASSERT_STR_EQ(a, b) do { \
	if (strcmp((a), (b)) != 0) { \
		fprintf(stderr, "\n[TEST] ASSERT_STR_EQ failed: \"%s\" != \"%s\" (%s:%d)\n", \
			(a), (b), __FILE__, __LINE__); \
		_ir0_test_failed = 1; \
	} \
} while (0)

#define ASSERT_NE(a, b) do { \
	if ((a) == (b)) { \
		fprintf(stderr, "\n[TEST] ASSERT_NE failed: %s != %s (%s:%d)\n", \
			#a, #b, __FILE__, __LINE__); \
		_ir0_test_failed = 1; \
	} \
} while (0)

#define TEST_EXIT() do { \
	if (_ir0_test_pass) { \
		fprintf(stderr, "[TEST] All %d test(s) passed.\n", _ir0_test_count); \
		exit(0); \
	} else { \
		fprintf(stderr, "[TEST] Some tests failed (total %d).\n", _ir0_test_count); \
		exit(1); \
	} \
} while (0)

#endif
