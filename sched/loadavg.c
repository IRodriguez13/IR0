/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: loadavg.c
 * Description: Global load average, as Linux kernel/sched/loadavg.c: every
 *              LOAD_FREQ the number of runnable tasks enters three
 *              exponentially decaying averages kept in FSHIFT fixed point.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <config.h>
#include <ir0/loadavg.h>
#include <ir0/process.h>
#include <string.h>

#define LOAD_FREQ (5 * CONFIG_TICK_RATE_HZ + 1)
/* 1/exp(5s/1min), 1/exp(5s/5min), 1/exp(5s/15min) in fixed point. */
#define EXP_1 1884
#define EXP_5 2014
#define EXP_15 2037

static unsigned long avenrun[3];
static uint64_t calc_load_ticks;

/* The idle task is never load, as in Linux (rq->idle is not nr_running). */
static int task_is_idle(const process_t *p)
{
	return p->mode == KERNEL_MODE && strcmp(p->comm, "idle") == 0;
}

unsigned int nr_running(void)
{
	const process_t *p;
	unsigned int n = 0;

	for (p = process_list; p; p = p->next)
	{
		if ((p->state == PROCESS_READY || p->state == PROCESS_RUNNING) &&
		    process_is_alive(p) && !task_is_idle(p))
			n++;
	}
	return n;
}

unsigned int nr_threads(void)
{
	const process_t *p;
	unsigned int n = 0;

	for (p = process_list; p; p = p->next)
		if (process_is_alive(p))
			n++;
	return n;
}

static unsigned long calc_load(unsigned long load, unsigned long exp,
			       unsigned long active)
{
	unsigned long newload = load * exp + active * (FIXED_1 - exp);

	if (active >= load)
		newload += FIXED_1 - 1;
	return newload / FIXED_1;
}

/*
 * Called on every tick. IR0 has no uninterruptible sleep, so the active
 * count is nr_running() alone (Linux adds nr_uninterruptible).
 */
void calc_global_load_tick(void)
{
	unsigned long active;

	if (++calc_load_ticks < LOAD_FREQ)
		return;
	calc_load_ticks = 0;
	active = (unsigned long)nr_running() * FIXED_1;
	avenrun[0] = calc_load(avenrun[0], EXP_1, active);
	avenrun[1] = calc_load(avenrun[1], EXP_5, active);
	avenrun[2] = calc_load(avenrun[2], EXP_15, active);
}

/* Linux get_avenrun(): the averages plus @offset, shifted left by @shift. */
void get_avenrun(unsigned long *loads, unsigned long offset, int shift)
{
	int i;

	for (i = 0; i < 3; i++)
		loads[i] = (avenrun[i] + offset) << shift;
}
