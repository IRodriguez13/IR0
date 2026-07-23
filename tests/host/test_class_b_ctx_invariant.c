/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_class_b_ctx_invariant.c
 * Description: Host tests — Class B + Linux-like pt_regs contract
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness_ir0.h"
#include <sched/class_b.h>
#include <stdint.h>

void test_class_b_ctx_invariant_matrix(void)
{
	ir0_mock_cs_rip_t a, b, c, d;

	TEST_BEGIN("class_b_ctx_invariant_matrix");

	/* A: user CS + user RIP → OK for user iretq (apply from pt_regs) */
	a = ir0_mock_class_b_user_iretq_ok();
	ASSERT(!ir0_mock_class_b_is_bad(&a));
	ASSERT(!process_cs_rip_kernel_ret_bad(IR0_MOCK_USER_CODE_SEL, 0x00401234ULL));

	/* B: Class B — kernel CS + user RIP → BAD */
	b = ir0_mock_class_b_bad();
	ASSERT(ir0_mock_class_b_is_bad(&b));
	ASSERT(process_cs_rip_kernel_ret_bad(IR0_MOCK_KERNEL_CODE_SEL, 0x00401234ULL));
	ASSERT(process_cs_rip_kernel_ret_bad(IR0_MOCK_KERNEL_CODE_SEL, IR0_USER_RIP_LO));
	ASSERT(process_cs_rip_kernel_ret_bad(IR0_MOCK_KERNEL_CODE_SEL, IR0_USER_RIP_HI));

	/* C: post-save — kernel CS + kernel .text RIP → OK for kernel_ret */
	c = ir0_mock_class_b_post_save();
	ASSERT(!ir0_mock_class_b_is_bad(&c));
	ASSERT(!process_cs_rip_kernel_ret_bad(IR0_MOCK_KERNEL_CODE_SEL, 0x00200000ULL));

	/* D: wait-like kernel resume IP (not userspace) → OK */
	d.cs = IR0_MOCK_KERNEL_CODE_SEL;
	d.rip = 0x00123456ULL;
	ASSERT(!ir0_mock_class_b_is_bad(&d));
	ASSERT(process_rip_in_user_range(0x00400000ULL));
	ASSERT(!process_rip_in_user_range(0x00101000ULL));

	/*
	 * E: Linux-like — soft sync OK with USER CS; Class B and
	 * want_kernel_ret+KERNEL_CS+user rip are not.
	 */
	ASSERT(ir0_mock_linux_pt_regs_contract_ok(IR0_MOCK_USER_CODE_SEL,
						  0x00401234ULL, 0));
	ASSERT(!ir0_mock_linux_pt_regs_contract_ok(IR0_MOCK_KERNEL_CODE_SEL,
						   IR0_MOCK_CLASS_B_USER_RIP, 0));
	ASSERT(!ir0_mock_linux_pt_regs_contract_ok(IR0_MOCK_KERNEL_CODE_SEL,
						   IR0_MOCK_CLASS_B_USER_RIP, 1));

	TEST_END();
}
