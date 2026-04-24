/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_string.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Tests de string (strlen, strcmp) sin proceso.
 */

#include "test/ktest_harness.h"
#include <string.h>

void ktest_string(void)
{
	KTEST_BEGIN("string");

	KASSERT_EQ(strlen(""), (size_t)0);
	KASSERT_EQ(strlen("a"), (size_t)1);
	KASSERT_EQ(strlen("abc"), (size_t)3);

	KASSERT_EQ(strcmp("", ""), 0);
	KASSERT_EQ(strcmp("a", "a"), 0);
	KASSERT(strcmp("a", "b") < 0);
	KASSERT(strcmp("b", "a") > 0);
	KASSERT(strcmp("ab", "abc") < 0);
	KASSERT(strcmp("abc", "ab") > 0);

	KTEST_END();
}
