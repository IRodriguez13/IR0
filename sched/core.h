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
 * CONFIG_SCHEDULER names the one policy file that is linked: sched/rr.c
 * ("rr"), sched/prio.c ("prio") or sched/cfs.c ("cfs"); the Makefile
 * registry sched-policy-<name> maps the name to the object. There is no
 * run-time dispatch. A policy implements the queue operations published in
 * <ir0/sched.h> (add, remove, promote, count, name, selftest) plus
 * sched_pick_next() below; the core (sched/core.c) owns schedule() and the
 * context switch. Policies never call each other.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/cpu.h>
#include <ir0/sched.h>

/*
 * Each policy file ends its includes with SCHED_POLICY_LINKED("<name>").
 * If the Makefile links sched/<name>.o while CONFIG_SCHEDULER (the C view
 * of the choice) names another policy, compilation of that file fails.
 */
#define SCHED_POLICY_LINKED(name) \
	_Static_assert(__builtin_strcmp(CONFIG_SCHEDULER, name) == 0, \
		       "sched/" name ".c is linked but CONFIG_SCHEDULER selects another policy")

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
