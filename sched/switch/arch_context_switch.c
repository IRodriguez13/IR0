/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_context_switch.c
 * Description: Portable switch_to() dispatcher — ISA body in arch_switch.c.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_switch.h>
#include <ir0/task.h>
#include <ir0/arch_task.h>
#include <ir0/process.h>
#include <ir0/switch.h>
#include <ir0/tls.h>

/*
 * Common pre-switch preparation.  It deliberately does not activate @next's
 * address-space root: the ISA transition still owns that unsafe window while
 * the outgoing kernel stack is live.  It only prepares state that is safe to
 * select before register/stack handoff.
 */
static void context_prepare_next(task_t *next)
{
	process_t *next_proc;

	if (!next)
		return;
	next_proc = task_to_process(next);
	if (!next_proc)
		return;

	if (task_mm_root(next) == 0 && process_pgd(next_proc))
		task_set_mm_root(next, (uint64_t)(uintptr_t)process_pgd(next_proc));
	set_current_kernel_stack(next_proc);
	set_tls(process_tls_get(next_proc));
}

void context_prepare_first(struct process *next)
{
	if (!next)
		return;
	context_prepare_next(&next->task);
}

void switch_to(task_t *prev, task_t *next)
{
	/* Preserve the outgoing user-stack shadow before selecting @next's stack. */
	if (prev)
		switch_save_user_rsp(task_to_process(prev));
	context_prepare_next(next);
	arch_switch_to(prev, next);
}
