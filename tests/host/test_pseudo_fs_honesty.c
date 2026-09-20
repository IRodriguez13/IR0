/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_pseudo_fs_honesty.c
 * Description: Host policy checks for pseudo-fs honesty contracts.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <stdint.h>

extern int sys_devices_cpu_online_write_reg(unsigned cpu, const char *buf, size_t count);
extern uint32_t sys_kernel_max_processes_limit(void);

static void test_pseudo_fs_cpu_online_write_eopnotsupp(void)
{
	int rc;

	TEST_BEGIN("sysfs cpu online write returns EOPNOTSUPP");
	rc = sys_devices_cpu_online_write_reg(0, "1\n", 2);
	ASSERT_EQ(rc, -95); /* EOPNOTSUPP / ENOTSUPP */
	TEST_END();
}

static void test_pseudo_fs_max_processes_limit_floor(void)
{
	uint32_t limit;

	TEST_BEGIN("sysfs max_processes limit is non-zero");
	limit = sys_kernel_max_processes_limit();
	ASSERT(limit >= 1U);
	TEST_END();
}

void test_pseudo_fs_honesty(void)
{
	test_pseudo_fs_cpu_online_write_eopnotsupp();
	test_pseudo_fs_max_processes_limit_floor();
}
