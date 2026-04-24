/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: kernel_memsafe_harness.h
 * Description: IR0 kernel source/header file
 */

/*
 * Harness mínimo para kernel-memsafe: macros de test.
 */

#ifndef KERNEL_MEMSAFE_HARNESS_H
#define KERNEL_MEMSAFE_HARNESS_H

#include <stdio.h>

extern int tests_run;
extern int tests_failed;

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

#endif
