/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Round-Robin Scheduler
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Public interface for round-robin scheduler
 */

#pragma once

#include "process.h"

/* ========================================================================== */
/* TYPES                                                                      */
/* ========================================================================== */

typedef struct rr_task
{
	process_t *process;
	struct rr_task *next;
} rr_task_t;

/* ========================================================================== */
/* PUBLIC API                                                                 */
/* ========================================================================== */

void rr_add_process(process_t *proc);
void rr_schedule_next(void);
