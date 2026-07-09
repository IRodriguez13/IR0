/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_musl_mmap_contract.c
 * Description: Host tests — musl/Linux mmap ABI contract (pure helpers)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <ir0/abi/mmap_contract.h>

void test_musl_mmap_contract(void)
{
	uintptr_t ok_va = IR0_USER_MMAP_START + 0x4000UL;
	uintptr_t err = (uintptr_t)(intptr_t)(-12);
	int errno_out = 0;
	uintptr_t libc_ret;

	TEST_BEGIN("musl_mmap_contract");

	/* Success VA passes through syscall + libc layers */
	ASSERT(!ir0_mmap_is_kernel_error(ok_va));
	ASSERT_EQ((int64_t)ok_va, ir0_mmap_syscall_ret(ok_va));
	libc_ret = ir0_mmap_libc_ret(ok_va, &errno_out);
	ASSERT_EQ(libc_ret, ok_va);
	ASSERT_EQ(errno_out, 0);

	/* ENOMEM from kernel → negative rax + libc MAP_FAILED */
	ASSERT(ir0_mmap_is_kernel_error(err));
	ASSERT_EQ(ir0_mmap_kernel_errno(err), 12);
	ASSERT_EQ(ir0_mmap_syscall_ret(err), (int64_t)-12);
	libc_ret = ir0_mmap_libc_ret(err, &errno_out);
	ASSERT_EQ(libc_ret, IR0_MMAP_FAILED);
	ASSERT_EQ(errno_out, 12);

	/* MAP_FIXED must use exact VA (not advisory) */
	ASSERT(ir0_mmap_fixed_requires_exact_va(IR0_MAP_FIXED | IR0_MAP_PRIVATE |
						IR0_MAP_ANONYMOUS));
	ASSERT(!ir0_mmap_fixed_requires_exact_va(IR0_MAP_PRIVATE |
						 IR0_MAP_ANONYMOUS));

	/* musl arena bounds */
	ASSERT(ir0_mmap_in_musl_arena(IR0_USER_MMAP_START, 0x1000UL,
				      IR0_USER_MMAP_START, 0x80010000UL));
	ASSERT(!ir0_mmap_in_musl_arena(IR0_USER_MMAP_START - 0x1000UL, 0x1000UL,
				       IR0_USER_MMAP_START, 0x80010000UL));

	/* Linear slot bump (0x8004000 + 4K → 0x8005000) */
	ASSERT_EQ(ir0_mmap_musl_next_slot(0x8004000UL, 0x1000UL), 0x8005000UL);
	ASSERT(ir0_mmap_page_aligned(0x8005000UL));

	TEST_END();
}
