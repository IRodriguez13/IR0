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
#include "backend.h"
#include "sched_switch.h"

static const struct ir0_sched_ops *sched_ops(void)
{
	return sched_backend_active();
}

void sched_add_process(process_t *proc)
{
	sched_ops()->add(proc);
}

void sched_remove_process(process_t *proc)
{
	sched_ops()->remove(proc);
}

void sched_schedule_next(void)
{
	sched_ops()->schedule_next();
}

void sched_switch_to(process_t *next)
{
	sched_context_switch_to(next);
}

void sched_reset(void)
{
	if (sched_ops()->reset)
		sched_ops()->reset();
}

int sched_count_runnable(void)
{
	return sched_ops()->count_runnable();
}

void sched_promote_process(process_t *proc)
{
	sched_ops()->promote(proc);
}

const char *sched_active_policy_name(void)
{
	return sched_ops()->name;
}

int sched_selftest(void)
{
	return sched_ops()->selftest ? sched_ops()->selftest() : 0;
}
