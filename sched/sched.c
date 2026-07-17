/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sched.c
 * Description: Scheduler facade dispatch via backend ops (CONFIG_SCHEDULER_POLICY).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/sched.h>
#include "sched_ops.h"
#include <config.h>

/*
 * Policy 0: round-robin ops.
 * Policy 1: CFS name only — still RR ops (honest alias; no cfs_sched.c / no rr include).
 * Policy 2: priority-band ops.
 */
#if CONFIG_SCHEDULER_POLICY == 2
static const struct ir0_sched_ops *const g_sched_ops = &ir0_priority_sched_ops;
#else
static const struct ir0_sched_ops *const g_sched_ops = &ir0_rr_sched_ops;
#endif

void sched_add_process(process_t *proc)
{
	g_sched_ops->add(proc);
}

void sched_remove_process(process_t *proc)
{
	g_sched_ops->remove(proc);
}

void sched_schedule_next(void)
{
	g_sched_ops->schedule_next();
}

int sched_count_runnable(void)
{
	return g_sched_ops->count_runnable();
}

void sched_promote_process(process_t *proc)
{
	g_sched_ops->promote(proc);
}

const char *sched_active_policy_name(void)
{
#if CONFIG_SCHEDULER_POLICY == 1
	return "cfs";
#elif CONFIG_SCHEDULER_POLICY == 2
	return "priority";
#else
	return "round_robin";
#endif
}
