/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_arch_task_contract.c
 * Description: arch_task_contract_suite — portable arg accessors (§3/§7).
 */

#include "test_harness_ir0.h"
#include <ir0/task.h>
#include <string.h>

void test_arch_task_contract(void)
{
	task_t t;

	memset(&t, 0, sizeof(t));
	task_set_arg0(&t, 0x1111);
	task_set_arg1(&t, 0x2222);
	task_set_arg2(&t, 0x3333);
	ASSERT_EQ(task_get_arg0(&t), 0x1111ULL);
	ASSERT_EQ(task_get_arg1(&t), 0x2222ULL);
	ASSERT_EQ(task_get_arg2(&t), 0x3333ULL);
	/* Deprecated x86-named wrappers must stay aliases. */
	ASSERT_EQ(task_get_rdi(&t), task_get_arg0(&t));
	ASSERT_EQ(task_get_rsi(&t), task_get_arg1(&t));
	ASSERT_EQ(task_get_rdx(&t), task_get_arg2(&t));
}
