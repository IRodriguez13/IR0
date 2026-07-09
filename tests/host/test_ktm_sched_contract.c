/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_ktm_sched_contract.c
 * Description: Host tests — KTM poll resume / sched contract predicates (pure logic)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness_ir0.h"
#include <stdint.h>

static int poll_arch_resume_ok(uint8_t irq_saved, void *poll_waiter,
			       uint8_t poll_resume_via_arch, void *wait_status)
{
	if (!irq_saved)
		return 1;
	if (wait_status)
		return 1;
	if (poll_waiter && poll_resume_via_arch)
		return 1;
	if (!poll_waiter && !poll_resume_via_arch)
		return 1;
	return 0;
}

void test_ktm_poll_arch_resume_matrix(void)
{
	TEST_BEGIN("ktm_poll_arch_resume_matrix");

	ASSERT(poll_arch_resume_ok(0, NULL, 0, NULL));
	ASSERT(poll_arch_resume_ok(1, (void *)1, 1, NULL));
	ASSERT(poll_arch_resume_ok(1, NULL, 0, NULL));
	ASSERT(!poll_arch_resume_ok(1, NULL, 1, NULL));
	ASSERT(poll_arch_resume_ok(1, NULL, 0, (void *)1));

	TEST_END();
}
