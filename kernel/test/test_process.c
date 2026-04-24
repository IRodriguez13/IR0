/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_process.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Tests de proceso actual y de boot
 */

#include "test/ktest_harness.h"
#include "process.h"
#include "syscalls.h"

void ktest_boot_ok(void)
{
	KTEST_BEGIN("boot_ok");
	KASSERT(1);
	KTEST_END();
}

void ktest_process_current(void)
{
	KTEST_BEGIN("process_current");
	KASSERT(current_process != NULL);
	KASSERT_GE(current_process->task.pid, 0);
	int64_t pid = sys_getpid();
	KASSERT_EQ(pid, (int64_t)current_process->task.pid);
	KTEST_END();
}
