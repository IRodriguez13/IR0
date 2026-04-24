/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: runner.c
 * Description: IR0 kernel source/header file
 */

/*
 * IR0 Kernel - kernel-memsafe test runner
 * Código del kernel compilado para host; Valgrind detecta leaks en esas rutas.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int tests_run;
int tests_failed;

#define KTEST_BEGIN(name) do { \
	tests_run++; \
	fprintf(stderr, "[KERNEL-MEMSAFE] %s ... ", (name)); \
	fflush(stderr); \
} while (0)

#define KTEST_END() do { \
	if (tests_failed) { \
		fprintf(stderr, "FAIL\n"); \
	} else { \
		fprintf(stderr, "PASS\n"); \
	} \
} while (0)

#define KASSERT(c) do { if (!(c)) { tests_failed = 1; fprintf(stderr, "\n  ASSERT failed: %s\n", #c); } } while (0)

extern void test_resource_registry(void);

int main(void)
{
	tests_run = 0;
	tests_failed = 0;

	fprintf(stderr, "[KERNEL-MEMSAFE] Running Valgrind over kernel code paths\n");
	fprintf(stderr, "[KERNEL-MEMSAFE] ----------------------------------------\n");

	test_resource_registry();

	fprintf(stderr, "[KERNEL-MEMSAFE] ----------------------------------------\n");
	if (tests_failed) {
		fprintf(stderr, "[KERNEL-MEMSAFE] Some tests failed (%d run)\n", tests_run);
		return 1;
	}
	fprintf(stderr, "[KERNEL-MEMSAFE] All %d test(s) passed.\n", tests_run);
	return 0;
}
