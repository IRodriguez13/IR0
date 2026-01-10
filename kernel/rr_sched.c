/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Round-Robin Scheduler
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Simple round-robin process scheduler
 */

#include "process.h"
#include "rr_sched.h"
#include <ir0/memory/kmem.h>
#include <ir0/oops.h>
#include <arch/x86-64/sources/user_mode.h>
#include <ir0/signals.h>
#include <ir0/context.h>


static rr_task_t *rr_head = NULL;
static rr_task_t *rr_tail = NULL;
static rr_task_t *rr_current = NULL;


void rr_add_process(process_t *proc)
{
	rr_task_t *node;

	if (!proc)
		return;

	node = kmalloc(sizeof(rr_task_t));
	if (!node)
		return;

	node->process = proc;
	node->next = NULL;

	if (!rr_head)
	{
		rr_head = rr_tail = node;
	}
	else
	{
		rr_tail->next = node;
		rr_tail = node;
	}

	proc->state = PROCESS_READY;
}


void rr_schedule_next(void)
{
	static int first = 1;
	process_t *prev;
	process_t *next;

	if (!rr_head)
		return;

	prev = current_process;

	/* Select next process in round-robin fashion */
	if (!rr_current)
		rr_current = rr_head;
	else
		rr_current = rr_current->next ? rr_current->next : rr_head;

	next = rr_current->process;
	if (!next)
		return;

	/* Avoid unnecessary context switch */
	if (!first && prev == next)
		return;

	/* Update process states */
	if (prev && prev->state == PROCESS_RUNNING)
		prev->state = PROCESS_READY;

	next->state = PROCESS_RUNNING;
	current_process = next;
	
	/* Handle pending signals before context switch */
	handle_signals();

	/* First context switch - jump to ring3 */
	if (first)
	{
		first = 0;
		jmp_ring3((void *)next->task.rip);
		panic("Returned from jmp_ring3");
	}

	/* Normal context switch */
	if (prev && next) 
	{
		switch_context_x64(&prev->task, &next->task);
	}
}
