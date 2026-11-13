/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: entry.c
 * Description: Exit work on the interrupt return to userspace (Linux
 *              irqentry_exit_to_user_mode()): preemption and asynchronous
 *              signal delivery for a task that runs without syscalls.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/context.h>
#include <ir0/entry.h>
#include <ir0/sched.h>
#include <ir0/signal_irq.h>
#include <ir0/task_ops.h>
#include <ir0/tls.h>
#include <kernel/process.h>

void irqentry_exit_to_user_mode(void *frame)
{
	process_t *p = current_process;
	struct sigcontext interrupted;

	if (!p || p->mode != USER_MODE || !frame)
		return;

	if (sched_take_resched())
		schedule();
	if (!signal_pending(p))
		return;

	/*
	 * Same path as rt_sigreturn: the interrupted state becomes the task's
	 * user state and its syscall frame, do_signal() may turn the frame into
	 * a handler entry, and the task returns through its full user state.
	 */
	signal_fill_sigcontext_from_irq_frame(&interrupted, frame);
	task_load_sigcontext(&p->task, &interrupted);
	signal_fill_syscall_frame_from_sigcontext(&p->syscall_frame, &interrupted);
	do_signal(p, &interrupted);
	process_apply_syscall_frame_to_task(&p->task, &p->syscall_frame,
					    signal_sigcontext_return_value(&interrupted));
	process_restore_user_task_segments(p);
	tls_restore_current();
	switch_to_user_task(&p->task);
}
