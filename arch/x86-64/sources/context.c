/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: context.c
 * Description: x86-64 __switch_to and kernel-entry stack (TSS) hooks.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/context_backend.h>
#include <ir0/process.h>
#include <ir0/fpu_backend.h>
#include <asm/context.h>

#include "tss.h"

extern uint64_t kernel_syscall_stack_top;

void set_current_kernel_stack(struct process *p)
{
	process_t *proc = (process_t *)p;

	if (!proc || !proc->kstack_top)
		return;

	kernel_syscall_stack_top = proc->kstack_top;
	tss_set_rsp0(proc->kstack_top);
}

void __switch_to(task_t *prev, task_t *next)
{
	fpu_switch(prev, next);
	switch_context_x64(prev, next);
}
