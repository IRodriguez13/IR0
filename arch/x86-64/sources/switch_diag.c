/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: switch_diag.c
 * Description: x86-64 kernel-return diagnostics used by switch assembly.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/fault.h>
#include <ir0/klog.h>
#include <ir0/oops.h>
#include <ir0/process.h>
#include <asm/context.h>
#include <string.h>

/* Called from switch_x64.asm when kernel_ret RIP is outside kernel .text. */
void switch_report_bad_ret(uint64_t rip, task_t *task)
{
	process_t *p = task ? task_to_process(task) : current_process;
	uint64_t cs = task ? (uint64_t)task_get_cs(task) : 0;
	uint64_t rsp = task ? task_get_sp(task) : 0;

	/*
	 * Must be visible at default serial level: a nested #DF during panic
	 * dump used to erase the only clue and blame dump_stack_trace.
	 */
	klog_notice_fmt("CTX",
			"CLASSIFY KERNEL_RET_BAD_RIP rip=%llx cs=%llx rsp=%llx "
			"task=%llx pid=%x",
			(unsigned long long)rip,
			(unsigned long long)cs,
			(unsigned long long)rsp,
			(unsigned long long)((uint64_t)(uintptr_t)task),
			(unsigned)(p ? (uint32_t)p->task.pid : 0));
	{
		struct ir0_fault fault;

		/* Software-detected: no CPU exception, so kind stays OTHER. */
		memset(&fault, 0, sizeof(fault));
		fault.ip = (uintptr_t)rip;
		fault.sp = (uintptr_t)rsp;
		fault.code_seg = cs;
		panic_note_fault(&fault, p ? (unsigned int)p->task.pid : 0,
				 p ? p->comm : "(none)");
	}

	panicex("kernel_ret RIP not in .text", PANIC_KERNEL_BUG, __FILE__, __LINE__,
		__func__);
}
