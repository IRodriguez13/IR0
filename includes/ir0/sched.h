/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sched.h
 * Description: Portable scheduler facade — no <sched/...> includes.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

typedef struct process process_t;

/* Run the next task the policy picks; returns when this task runs again. */
void schedule(void);

/* Switch to @next directly (the exit path hands the CPU to idle). */
void sched_switch_to(process_t *next);

/*
 * Consume the timer's reschedule request. Returns 1 when the request was
 * pending and another task can run; the caller then calls schedule().
 */
int sched_take_resched(void);

/* Runqueue membership, implemented by the linked policy. */
void sched_add(process_t *proc);
void sched_remove(process_t *proc);
/* Make @proc READY and the policy's next candidate. */
void sched_promote(process_t *proc);
int sched_count_runnable(void);

const char *sched_policy_name(void);
/* Policy self-check at boot; 0 when it holds. */
int sched_selftest(void);
