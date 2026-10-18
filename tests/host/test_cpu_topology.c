/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * UP CPU topology contract tests.
 */

#include "test_harness.h"
#include <ir0/cpu.h>

void test_cpu_topology_up_contract(void)
{
	TEST_BEGIN("cpu topology exposes one consistent boot CPU");

	ASSERT(cpu_id() == 0);
	ASSERT(cpu_possible_mask() == 1);
	ASSERT(cpu_present_mask() == 1);
	ASSERT(cpu_online_mask() == 1);
	ASSERT(cpu_active_mask() == 1);
	ASSERT(cpu_online_count() == 1);
	ASSERT(cpu_is_possible(0));
	ASSERT(cpu_is_present(0));
	ASSERT(cpu_is_online(0));
	ASSERT(cpu_is_active(0));
	ASSERT(!cpu_is_possible(1));
	ASSERT(!cpu_is_present(63));
	ASSERT(!cpu_is_online(64));
	ASSERT(!cpu_is_active(255));

	TEST_END();
}
