/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_early_process_abi.c
 * Description: Host checks for staged PID 1 session / process-group identity
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <ir0/early_process.h>

void test_early_process_abi(void)
{
	TEST_BEGIN("early_process_abi");

	ASSERT_EQ(ir0_early_process_getpgid(0), 1);
	ASSERT_EQ(ir0_early_process_getpgid(1), 1);
	ASSERT_EQ(ir0_early_process_getpgid(2), -IR0_EARLY_ESRCH);
	ASSERT_EQ(ir0_early_process_getsid(0), 1);
	ASSERT_EQ(ir0_early_process_getsid(9), -IR0_EARLY_ESRCH);
	ASSERT_EQ(ir0_early_process_setsid(), -IR0_EARLY_EPERM);
	ASSERT_EQ(ir0_early_process_setpgid(0, 0), -IR0_EARLY_EPERM);
	ASSERT_EQ(ir0_early_process_setpgid(1, 1), -IR0_EARLY_EPERM);
	ASSERT_EQ(ir0_early_process_setpgid(2, 2), -IR0_EARLY_ESRCH);
	ASSERT_EQ(ir0_early_process_kill(1, 0), 0);
	ASSERT_EQ(ir0_early_process_kill(0, 15), 0);
	ASSERT_EQ(ir0_early_process_kill(-1, 9), -IR0_EARLY_ESRCH);
	ASSERT_EQ(ir0_early_process_kill(2, 9), -IR0_EARLY_ESRCH);
	ASSERT_EQ(ir0_early_process_kill(1, 65), -IR0_EARLY_EINVAL);
	ASSERT_EQ(ir0_early_process_tkill(1, 14), 0);
	ASSERT_EQ(ir0_early_process_tkill(2, 14), -IR0_EARLY_ESRCH);
	ASSERT_EQ(ir0_early_process_tgkill(1, 1, 2), 0);
	ASSERT_EQ(ir0_early_process_tgkill(2, 1, 2), -IR0_EARLY_ESRCH);
	ASSERT(ir0_early_clone_flags_ok(CLONE_VM | CLONE_VFORK |
				       IR0_EARLY_SIGCHLD));
	ASSERT(ir0_early_clone_flags_ok(CLONE_VM | IR0_EARLY_SIGCHLD));
	ASSERT(!ir0_early_clone_flags_ok(IR0_EARLY_SIGCHLD));
	ASSERT(!ir0_early_clone_flags_ok(CLONE_VM | CLONE_THREAD));
	ASSERT(!ir0_early_clone_flags_ok(CLONE_VM | 0x00020000UL));

	TEST_END();
}
