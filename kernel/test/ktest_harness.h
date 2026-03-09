/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Test harness (in-kernel)
 * Batería exhaustiva: ASSERT y logging vía serial para tests dentro del kernel.
 */

#ifndef _KERNEL_KTEST_HARNESS_H
#define _KERNEL_KTEST_HARNESS_H

#include <drivers/serial/serial.h>
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
