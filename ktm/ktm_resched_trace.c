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
#include <ir0/serial_io.h>

#ifdef IR0_KERNEL_TESTS

void ktm_sched_trace_wake(const process_t *p, const char *tag)
{
	serial_print("[KTM][SCHED_WAKE] tag=");
	serial_print(tag ? tag : "?");
	serial_print(" pid=");
	serial_print_hex32(p ? (uint32_t)p->task.pid : 0);
	serial_print(" state=");
	serial_print_hex64(p ? (uint64_t)p->state : 0);
	serial_print("\n");
}

void ktm_sched_trace_pick(const process_t *prev, const process_t *next)
{
	serial_print("[KTM][SCHED_PICK] prev_pid=");
	serial_print_hex32(prev ? (uint32_t)prev->task.pid : 0);
	serial_print(" prev_state=");
	serial_print_hex64(prev ? (uint64_t)prev->state : 0);
	serial_print(" next_pid=");
	serial_print_hex32(next ? (uint32_t)next->task.pid : 0);
	serial_print(" next_state=");
	serial_print_hex64(next ? (uint64_t)next->state : 0);
	serial_print("\n");
}

#endif /* IR0_KERNEL_TESTS */
