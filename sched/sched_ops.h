/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sched_ops.h
 * Description: Backend ops table (sched/ only — not a portable include).
 *
 * Contract (API layers):
 * - Portable callers use <ir0/sched.h> only (facade).
 * - Backends implement ir0_sched_ops; they must NOT #include or call the
 *   other backend's private symbols (e.g. rr_* from priority, or vice versa).
 * - After picking @next, backends call sched_context_switch_to() — never
 *   another backend's runqueue helpers.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include "process.h"

struct ir0_sched_ops
{
	void (*add)(process_t *proc);
	void (*remove)(process_t *proc);
	void (*schedule_next)(void);
	int (*count_runnable)(void);
	void (*promote)(process_t *proc);
};

extern const struct ir0_sched_ops ir0_rr_sched_ops;
extern const struct ir0_sched_ops ir0_priority_sched_ops;
