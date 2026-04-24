/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: scheduler_api.h
 * Description: IR0 kernel source/header file
 */

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
