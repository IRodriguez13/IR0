/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: signal_enter.c
 * Description: signal_enter_pending lifecycle (handler frame armed / cleared).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <string.h>

int process_signal_enter_pending(const process_t *p)
{
	if (!p)
		return 0;
	return p->signal_enter_pending != 0;
}

void process_signal_enter_pending_set(process_t *p)
{
	if (p)
		p->signal_enter_pending = 1;
}

void process_signal_enter_pending_clear(process_t *p)
{
	if (p)
		p->signal_enter_pending = 0;
}

void process_signal_enter_pending_init(process_t *p)
{
	process_signal_enter_pending_clear(p);
}

int process_signal_defer_catchable(const process_t *p)
{
	if (!p)
		return 0;
	return p->signal_defer_catchable != 0;
}

void process_signal_defer_catchable_set(process_t *p)
{
	if (p)
		p->signal_defer_catchable = 1;
}

void process_signal_defer_catchable_clear(process_t *p)
{
	if (p)
		p->signal_defer_catchable = 0;
}

void process_signal_last_delivered_set(process_t *p, int sig)
{
	if (!p || sig < 1 || sig >= _NSIG)
		return;
	p->signal_last_delivered = (uint8_t)sig;
}

int process_signal_last_delivered(const process_t *p)
{
	if (!p)
		return 0;
	return (int)p->signal_last_delivered;
}

void process_signal_last_delivered_clear(process_t *p)
{
	if (p)
		p->signal_last_delivered = 0;
}

void process_kernel_sleep_interrupted_clear(process_t *p)
{
	if (!p)
		return;
	p->kernel_sleep_interrupted = 0;
	memset(&p->kernel_sleep_syscall_frame, 0,
	       sizeof(p->kernel_sleep_syscall_frame));
}

void process_kernel_sleep_interrupted_backup_frame(process_t *p)
{
	if (!p || p->coop_resched_resume)
		return;

	if (p->kernel_syscall_sleep)
	{
		/*
		 * Frame snapshotted in process_arm_kernel_syscall_sleep; do not
		 * overwrite here — syscall_frame may already be handler residue.
		 */
		p->kernel_sleep_interrupted = 1;
		return;
	}

	/*
	 * A fresh syscall frame alone does not mean that the syscall blocked.
	 * Timer signals commonly arrive at the return boundary of clock_gettime,
	 * close, or write. Treating those as interrupted sleeps made rt_sigreturn
	 * re-execute the completed syscall; TinyX consequently closed a live X11
	 * client fd when SIGALRM landed after close(2). Blocking paths must arm
	 * kernel_syscall_sleep before scheduling, which is the sole restart proof.
	 */
	if (p->syscall_frame_fresh &&
	    (int64_t)p->syscall_resume_rax == -(int64_t)EINTR)
	{
		/*
		 * Some wait paths observe a pending signal immediately, before they
		 * schedule and arm kernel_syscall_sleep.  Their completed result is
		 * explicitly -EINTR, which is sufficient proof that sigreturn must
		 * apply SA_RESTART policy to the captured entry frame.
		 */
		process_kernel_sleep_capture_syscall_frame(p);
		p->kernel_sleep_interrupted = 1;
	}
}

int process_kernel_sleep_interrupted(const process_t *p)
{
	if (!p)
		return 0;
	return p->kernel_sleep_interrupted != 0;
}
