/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: rr_sched.h
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Round-Robin Scheduler
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Public interface for round-robin scheduler
 */

#pragma once

#include "process.h"

typedef struct rr_task
{
	process_t *process;
	struct rr_task *next;
} rr_task_t;

void rr_add_process(process_t *proc);
void rr_remove_process(process_t *proc);
void rr_schedule_next(void);
