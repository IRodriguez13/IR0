/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_architecture_guard.c
 * Description: Host test — scripts/architecture_guard.py must exit 0.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <stdlib.h>
#include <sys/wait.h>

void test_architecture_guard_py(void)
{
	int rc;
	int status;

	TEST_BEGIN("architecture_guard_py");
	rc = system("python3 ../../scripts/architecture_guard.py");
	ASSERT(rc != -1);
	status = WEXITSTATUS(rc);
	ASSERT_EQ(status, 0);
	TEST_END();
}
