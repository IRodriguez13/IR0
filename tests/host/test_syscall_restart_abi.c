/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_syscall_restart_abi.c
 * Description: Host ABI — a restarted x86-64 syscall re-issues the same call.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness_ir0.h"
#include <ir0/syscall_frame.h>
#include <stdint.h>
#include <string.h>

/*
 * Linux x86 restart: the frame goes back over the 2-byte syscall
 * instruction and rax holds the syscall number again, while the argument
 * registers keep their entry values.
 */
void test_syscall_restart_frame_abi(void)
{
	arch_syscall_frame_t regs;

	TEST_BEGIN("syscall_restart_frame_abi");

	memset(&regs, 0, sizeof(regs));
	regs.rip = 0x4017b2ULL;
	regs.rdi = 0x7fdf8000ULL;
	regs.rsi = 1000ULL;
	regs.orig_rax = 61ULL; /* wait4 */
	regs.rax = regs.orig_rax;

	syscall_frame_set_return(&regs, (uint64_t)(int64_t)-512);
	ASSERT_EQ(syscall_frame_return(&regs), (uint64_t)(int64_t)-512);

	syscall_frame_restart(&regs);
	ASSERT_EQ(regs.rip, 0x4017b0ULL);
	ASSERT_EQ(syscall_frame_return(&regs), 61ULL);
	ASSERT_EQ(regs.rdi, 0x7fdf8000ULL);
	ASSERT_EQ(regs.rsi, 1000ULL);

	TEST_END();
}
