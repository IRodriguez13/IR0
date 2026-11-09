/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: core.h
 * Description: Scheduler core <-> policy contract (sched/ only, not a facade).
 *
 * CONFIG_SCHEDULER_POLICY links exactly one policy file: sched/rr.c
 * (policies 0 and 1) or sched/prio.c (policy 2). There is no run-time
 * dispatch. A policy implements the queue operations published in
 * <ir0/sched.h> (add, remove, promote, count, name, selftest) plus
 * sched_pick_next() below; the core (sched/core.c) owns schedule() and the
 * context switch. Policies never call each other.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/cpu.h>
#include <ir0/sched.h>

/*
 * Choose the next task to run, or NULL when nothing is runnable. Takes the
 * runqueue lock itself; the caller switches to the result.
 */
process_t *sched_pick_next(void);

/*
 * Runqueue lock. On UP, masking interrupts serializes runqueue changes
 * against timer-driven scheduling. A multi-CPU build implements this pair
 * with the per-CPU runqueue spinlock in smp/; neither the core nor the
 * policies change.
 */
static inline unsigned long rq_lock(void)
{
	return irq_save();
}

static inline void rq_unlock(unsigned long flags)
{
	irq_restore(flags);
}
