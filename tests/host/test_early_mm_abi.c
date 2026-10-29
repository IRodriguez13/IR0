/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_early_mm_abi.c
 * Description: Staged MM arena follows the loaded image, not a program name
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <ir0/early_mm.h>
#include <ir0/early_syscall.h>
#include <ir0/syscall_id.h>

#include <ir0/errno.h>

static int host_map_user_page(uint64_t page, int executable)
{
	(void)page;
	(void)executable;
	return 0;
}

static void host_zero_user_page(uint64_t page)
{
	(void)page;
}

static const struct early_mm_services g_mm = {
	.map_user_page = host_map_user_page,
	.zero_user_page = host_zero_user_page,
};

static const struct early_syscall_services g_services = {
	.mm = &g_mm,
};

static int64_t call_brk(uint64_t request)
{
	struct early_syscall_context context;

	context.leave_usermode = 0;
	context.smoke_ok = 0;
	context.services = &g_services;
	return early_mm_provider.handler(&context, IR0_SYSCALL_BRK, request, 0, 0,
					 0, 0, 0);
}

void test_early_mm_abi(void)
{
	struct early_mm_layout hello;
	struct early_mm_layout pid1;

	TEST_BEGIN("early_mm_abi");

	ASSERT_EQ(call_brk(0), -ENOSYS);

	hello.heap_base = 0x43001000UL;
	hello.map_base = 0x431a0000UL;
	hello.map_limit = 0x43200000UL;
	early_mm_activate(&hello);
	ASSERT_EQ(call_brk(0), (int64_t)0x43001000UL);

	pid1.heap_base = 0x44027000UL;
	pid1.map_base = 0x44200000UL;
	pid1.map_limit = 0x44800000UL;
	early_mm_activate(&pid1);
	ASSERT_EQ(call_brk(0), (int64_t)0x44027000UL);
	ASSERT_EQ(call_brk(0x44028000UL), (int64_t)0x44028000UL);

	early_mm_activate(0);
	ASSERT_EQ(call_brk(0), -ENOSYS);

	TEST_END();
}
