/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: priority_sched.h
 * Description: Priority-band scheduler backend (CONFIG_SCHEDULER_POLICY==2).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include "process.h"

void priority_add_process(process_t *proc);
void priority_remove_process(process_t *proc);
void priority_schedule_next(void);
int priority_count_runnable(void);
void priority_promote_process(process_t *proc);
int priority_sched_selftest(void);
