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
#include <ir0/arch_cpu.h>
#include <sched/task.h>
#include <ir0/process.h>
#include "switch_early.h"

void first_switch_to(struct process *next)
{
	process_t *p = (process_t *)next;
	uint64_t root;

	if (!p)
		panic("first_switch_to: null process");

	root = process_mm_root(p);
	if (!root)
		panic("first_switch_to: null mm root");

	/*
	 * Match x86 first_switch_to: a USER_MODE init image must exception-
	 * return to EL0. Cooperative EL1 switch only RETs via x30, which is
	 * unset on a freshly spawned ELF.
	 */
	if (p->mode == USER_MODE)
	{
		switch_to_user_task(&p->task);
		panic("Returned from first_switch_to user (ARM64)");
	}

	mm_activate((uintptr_t)root);
	switch_context_arm64(NULL, &p->task);
	panic("Returned from first_switch_to (ARM64) unexpectedly");
}
