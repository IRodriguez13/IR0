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
 * Batería exhaustiva: ASSERT y logging vía serial para tests dentro del kernel.
 */

#ifndef _KERNEL_KTEST_HARNESS_H
#define _KERNEL_KTEST_HARNESS_H

#include <ir0/serial_io.h>
#include <stdint.h>

extern int _ktest_failed;
extern int _ktest_count;
extern int _ktest_pass;

#define KTEST_BEGIN(name) do { \
	_ktest_failed = 0; \
	_ktest_count++; \
	serial_print("[KTEST] "); \
	serial_print(name); \
	serial_print(" ... "); \
} while (0)

#define KTEST_END() do { \
	if (_ktest_failed) { \
		serial_print("FAIL\n"); \
		_ktest_pass = 0; \
	} else { \
		serial_print("PASS\n"); \
	} \
} while (0)

#define KASSERT(cond) do { \
	if (!(cond)) { \
		serial_print("\n[KTEST] ASSERT failed: "); \
		serial_print(#cond); \
		serial_print(" (line "); \
		serial_print_hex32((uint32_t)__LINE__); \
		serial_print(")\n"); \
		_ktest_failed = 1; \
	} \
} while (0)

#define KASSERT_EQ(a, b) do { \
	if ((a) != (b)) { \
		serial_print("\n[KTEST] ASSERT_EQ failed: "); \
		serial_print(#a); \
		serial_print(" != "); \
		serial_print(#b); \
		serial_print(" (line "); \
		serial_print_hex32((uint32_t)__LINE__); \
		serial_print(")\n"); \
		_ktest_failed = 1; \
	} \
} while (0)

#define KASSERT_GE(a, b) do { \
	if ((a) < (b)) { \
		serial_print("\n[KTEST] ASSERT_GE failed\n"); \
		_ktest_failed = 1; \
	} \
} while (0)

#define KASSERT_GT(a, b) do { \
	if ((a) <= (b)) { \
		serial_print("\n[KTEST] ASSERT_GT failed\n"); \
		_ktest_failed = 1; \
	} \
} while (0)

void kernel_test_run_all(void);

#endif
