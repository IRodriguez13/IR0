/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: switch_diag.c
 * Description: x86-64 kernel-return diagnostics used by switch assembly.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_switch.h>
#include <ir0/process.h>
#include <ir0/task.h>
#include <ir0/klog.h>
#include <ir0/oops.h>
#include <ir0/vga.h>

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
	print("[CTX] CLASSIFY KERNEL_RET_BAD_RIP rip=");
	print_hex64(rip);
	print(" cs=");
	print_hex64(cs);
	print(" pid=");
	print_hex((uintptr_t)(p ? (uint32_t)p->task.pid : 0));
	print("\n");

	panic_note_exception_frame(
		0 /* software */, 0, rip, cs, 0, rsp, 0, 0,
		p ? (uint32_t)p->task.pid : 0,
		p ? p->comm : "(none)");

	panicex("kernel_ret RIP not in .text", PANIC_KERNEL_BUG, __FILE__, __LINE__,
		__func__);
}
