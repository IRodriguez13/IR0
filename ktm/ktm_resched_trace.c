/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_resched_trace.c
 * Description: IR0 kernel source — ktm resched trace
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm.h>
#include <ir0/process.h>
#include <ir0/ktm/klog.h>

#ifdef IR0_KERNEL_TESTS

void ktm_sched_trace_wake(const process_t *p, const char *tag)
{
	klog_debug_fmt("KTM",
		       "[KTM][SCHED_WAKE] tag=%s pid=%x state=%llx",
		       tag ? tag : "?", (unsigned)(p ? (uint32_t)p->task.pid : 0),
		       (unsigned long long)(p ? (uint64_t)p->state : 0));
}

void ktm_sched_trace_pick(const process_t *prev, const process_t *next)
{
	klog_debug_fmt("KTM",
		       "[KTM][SCHED_PICK] prev_pid=%x prev_state=%llx next_pid=%x next_state=%llx",
		       (unsigned)(prev ? (uint32_t)prev->task.pid : 0),
		       (unsigned long long)(prev ? (uint64_t)prev->state : 0),
		       (unsigned)(next ? (uint32_t)next->task.pid : 0),
		       (unsigned long long)(next ? (uint64_t)next->state : 0));
}

#endif /* IR0_KERNEL_TESTS */
