/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_oom_hook.c
 * Description: Reclaim-before-kill runs inside the booted kernel (kernel.ktest).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test/ktest_harness.h"
#include "process.h"
#include <ir0/errno.h>
#include <ir0/oom.h>
#include <ir0/reclaim.h>

/*
 * The holder is a kernel task. The hook ignores that mode, so the case
 * borrows USER_MODE for the call and puts it back. A missing reclaim return
 * SIGKILLs current and process_exit() does not come back, so the suite
 * never prints the pass line.
 */
static uint64_t oom_stock_left;

static uint64_t oom_stock_count(void *ctx)
{
	(void)ctx;
	return oom_stock_left;
}

static int oom_stock_scan(void *ctx, uint64_t target, uint64_t *reclaimed)
{
	(void)ctx;
	*reclaimed = oom_stock_left < target ? oom_stock_left : target;
	oom_stock_left -= *reclaimed;
	return 0;
}

static const struct ir0_reclaimer oom_stock = {
	.name = "ktest-oom",
	.count = oom_stock_count,
	.scan = oom_stock_scan,
	.ctx = &oom_stock_left,
};

void ktest_oom_reclaim_before_kill(void)
{
	process_mode_t saved;
	uint64_t kills;
	int rc;

	KTEST_BEGIN("oom_reclaim_before_kill");
	KASSERT(current_process != NULL);
	if (!current_process)
	{
		KTEST_END();
		return;
	}

	rc = mm_reclaimer_register(&oom_stock);
	KASSERT(rc == 0 || rc == -EEXIST);
	if (rc != 0 && rc != -EEXIST)
	{
		KTEST_END();
		return;
	}
	oom_stock_left = 1;
	kills = oom_kill_count();
	saved = current_process->mode;
	current_process->mode = USER_MODE;
	rc = oom_handle_alloc_failure(current_process, "ktest-demand-zero");
	current_process->mode = saved;

	KASSERT_EQ(rc, 0);
	KASSERT_EQ(oom_stock_left, (uint64_t)0);
	KASSERT_EQ(oom_kill_count(), kills);
	KASSERT_EQ(current_process->signal_pending, (uint32_t)0);
	KTEST_END();
}
