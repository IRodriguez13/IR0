/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: context_switch.c
 * Description: Portable switch_to() state preparation and backend dispatch.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "switch_internal.h"
#include <ir0/arch_mm.h>
#include <ir0/context.h>
#include <ir0/context_backend.h>
#include <ir0/process.h>
#include <ir0/percpu.h>
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
	if (task_mm_root(next))
		mm_prepare_root_activation(
			(address_space_root_t)task_mm_root(next));
	set_current_kernel_stack(next_proc);
	cpu_local_set_kernel_stack(next_proc->kstack_top);
	set_tls(process_tls_get(next_proc));
}

void switch_to(task_t *prev, task_t *next)
{
	context_prepare_next(next);
	if (!prev)
	{
		first_switch_to(task_to_process(next));
		return;
	}
	context_prepare_kernel_resume(next);
	__switch_to(prev, next);
}
