/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktest_bridge.c
 * Description: Report the legacy in-kernel ktest suite through KTM.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/ktm/scenario.h>
#include <stddef.h>

/*
 * kernel/test is only linked into kernel-x64-test.bin. A weak reference keeps
 * this object safe in every kernel: production resolves it to NULL and never
 * registers the scenario, while the test kernel registers it and boot_init
 * runs it from a holder process (ktest needs current_process).
 */
extern int kernel_test_run_all(void) __attribute__((weak));

static int ktest_bridge_run(ktm_context_t *ctx)
{
	(void)ctx;
	if (!kernel_test_run_all)
		return -1;
	return kernel_test_run_all();
}

static const ktm_scenario_t scenario_ktest = {
	.name = "kernel.ktest",
	.flags = 0,
	.setup = NULL,
	.run = ktest_bridge_run,
	.teardown = NULL,
};

void ktm_scenario_register_ktest_bridge(void)
{
	if (kernel_test_run_all)
		(void)ktm_scenario_register(&scenario_ktest);
}
