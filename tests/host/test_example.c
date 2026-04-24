/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_example.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Tests de humo del harness
 */

#include "test_harness.h"

void test_harness_smoke(void)
{
	TEST_BEGIN("harness_smoke");
	ASSERT(1);
	TEST_END();
}

void test_example_asserts(void)
{
	TEST_BEGIN("example_asserts");
	ASSERT(2 + 2 == 4);
	ASSERT_EQ(1, 1);
	ASSERT_NE(0, 1);
	ASSERT_STR_EQ("a", "a");
	TEST_END();
}
