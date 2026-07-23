/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: first_switch.c
 * Description: ARM64 first context switch behind first_switch_to.
 */

#include <arch/common/arch_portable.h>
#include <ir0/oops.h>
#include <sched/task.h>
#include <ir0/process.h>

extern void switch_context_arm64(task_t *prev, task_t *next);

void first_switch_to(struct process *next)
{
	process_t *p = (process_t *)next;
	uint64_t root;

	if (!p)
		panic("first_switch_to: null process");

	root = process_mm_root(p);
	if (!root)
		panic("first_switch_to: null mm root");

	mm_activate((uintptr_t)root);
	switch_context_arm64(NULL, &p->task);
	panic("Returned from first_switch_to (ARM64) unexpectedly");
}
