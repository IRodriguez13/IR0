/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Scheduler backend API
 *
 * Build-time scheduler abstraction entrypoints used by core subsystems.
 */

#pragma once

#include "process.h"

void sched_add_process(process_t *proc);
void sched_remove_process(process_t *proc);
void sched_schedule_next(void);
const char *sched_active_policy_name(void);
