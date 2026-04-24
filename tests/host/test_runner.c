/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_runner.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Test runner
 * Ejecuta todos los tests registrados y termina con código 0 (éxito) o 1 (fallo).
 */

#include "test_harness.h"
#include <stdio.h>

int _ir0_test_failed;
int _ir0_test_count;
int _ir0_test_pass;

extern void test_harness_smoke(void);
extern void test_example_asserts(void);

static void (*test_functions[])(void) = {
	test_harness_smoke,
	test_example_asserts,
	NULL
};

int main(void)
{
	_ir0_test_count = 0;
	_ir0_test_pass = 1;

	fprintf(stderr, "[TEST] IR0 kernel test suite\n");
	fprintf(stderr, "[TEST] --------------------\n");

	int n = 0;
	for (int i = 0; test_functions[i] != NULL; i++) {
		test_functions[i]();
		n++;
	}
	_ir0_test_count = n;  /* Asegurar total para TEST_EXIT */
	TEST_EXIT();
	return 1;
}
