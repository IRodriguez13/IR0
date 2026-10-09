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
#include <ir0/paging.h>
#include <ir0/vga.h>
#include <config.h>

#include "switch_diag.h"

void switch_audit_user_frame_resume(process_t *prev_proc,
                                    process_t *next_proc,
                                    task_t *next)
{
#if !IR0_DEBUG_WAIT
	(void)prev_proc;
	(void)next_proc;
	(void)next;
#else
	klog_print("WAIT CTX prev_pid=");
	klog_hex32(prev_proc ? (uint32_t)prev_proc->task.pid : 0);
	klog_print(" prev_state=");
	klog_hex64(prev_proc ? (uint64_t)prev_proc->state : 0);
	klog_print(" prev_irq_saved=");
	klog_hex64(prev_proc ? (uint64_t)prev_proc->irq_frame_saved : 0);
	klog_print(" next_pid=");
	klog_hex32(next_proc ? (uint32_t)next_proc->task.pid : 0);
	klog_print(" next_state=");
	klog_hex64(next_proc ? (uint64_t)next_proc->state : 0);
	klog_print(" next_irq_saved=");
	klog_hex64(next_proc ? (uint64_t)next_proc->irq_frame_saved : 0);
	klog_print(" next_cr3=");
	klog_hex64(next ? task_mm_root(next) : 0);
	klog_print(" active_cr3=");
	klog_hex64(paging_current_address_space());
	klog_print("\n");

	if (prev_proc && prev_proc->state == PROCESS_ZOMBIE)
		klog_info("WAIT", "CLASSIFY SCHED_SELECTED_ZOMBIE note=prev_is_zombie_on_switch");
	if (prev_proc && prev_proc->irq_frame_saved &&
	    (!next_proc || next_proc->state != PROCESS_BLOCKED))
	{
		klog_info("WAIT", "CLASSIFY WAITPID_PARENT_CONTEXT_CORRUPT reason=prev_irq_saved_but_next_not_blocked");
	}
	if (prev_proc && prev_proc->irq_frame_saved && next_proc &&
	    next_proc->irq_frame_saved == 0)
	{
		klog_info("WAIT", "CLASSIFY WAITPID_PARENT_CONTEXT_CORRUPT reason=resume_triggered_by_prev_irq_not_next");
	}

	if (next_proc)
	{
		uint64_t rip = task_get_ip(&next_proc->task);
		uint64_t rsp = task_get_sp(&next_proc->task);
		uint16_t cs = task_get_cs(&next_proc->task);
		uint16_t ss = task_get_ss(&next_proc->task);

		klog_print("WAIT CTX next_user_frame rip=");
		klog_hex64(rip);
		klog_print(" rsp=");
		klog_hex64(rsp);
		klog_print(" cs=");
		klog_hex64((uint64_t)cs);
		klog_print(" ss=");
		klog_hex64((uint64_t)ss);
		klog_print(" rflags=");
		klog_hex64(task_get_flags(&next_proc->task));
		klog_print(" rax=");
		klog_hex64(task_get_retval(&next_proc->task));
		klog_print("\n");

		if (task_mm_root(next) == 0 && process_pgd(next_proc))
			klog_info("WAIT", "CLASSIFY PARENT_CR3_BAD reason=task_cr3_zero");
		if (rip < 0x00400000ULL || rip > 0x00007FFFFFFFFFFFULL)
			klog_info("WAIT", "CLASSIFY PARENT_IRET_FRAME_BAD_RIP");
		if (rsp < 0x00400000ULL || rsp > 0x00007FFFFFFFFFFFULL)
			klog_info("WAIT", "CLASSIFY PARENT_IRET_FRAME_BAD_RSP");
		if (!task_cs_is_user(&next_proc->task) || (ss & 3u) != 3u)
			klog_info("WAIT", "CLASSIFY PARENT_IRET_FRAME_BAD_CS_SS");
	}
#endif
}

void switch_trace_user_frame_resume(process_t *prev_proc,
                                    process_t *next_proc,
                                    task_t *next)
{
#if !IR0_DEBUG_WAIT
	(void)prev_proc;
	(void)next_proc;
	(void)next;
#else
	syscall_user_frame_t *frame = next_proc ? &next_proc->syscall_frame : NULL;
	uintptr_t active_cr3_before = paging_current_address_space();
	uint64_t active_cr3_after_expected = next ? task_mm_root(next) : 0;
	uint64_t next_task_cr3 = next ? task_mm_root(next) : 0;
	uint64_t current_before = (uint64_t)(uintptr_t)current_process;
	uint64_t frame_addr = (uint64_t)(uintptr_t)frame;
	uint64_t next_proc_addr = (uint64_t)(uintptr_t)next_proc;
	uint64_t next_proc_end = next_proc_addr + sizeof(process_t);
	int frame_in_next_proc = frame_addr >= next_proc_addr &&
	                         frame_addr < next_proc_end;
	int frame_in_kernel = frame_addr < 0x00400000ULL ||
	                      frame_addr > 0x00007FFFFFFFFFFFULL;

	klog_print("CTX RESUME prev_pid=");
	klog_hex32(prev_proc ? (uint32_t)prev_proc->task.pid : 0);
	klog_print(" next_pid=");
	klog_hex32(next_proc ? (uint32_t)next_proc->task.pid : 0);
	klog_print(" current=");
	klog_hex64(current_before);
	klog_print(" active_cr3_before=");
	klog_hex64(active_cr3_before);
	klog_print(" active_cr3_pre_iret=");
	klog_hex64(paging_current_address_space());
	klog_print(" active_cr3_after_expected=");
	klog_hex64(active_cr3_after_expected);
	klog_print(" next_task_cr3=");
	klog_hex64(next_task_cr3);
	klog_print(" frame=");
	klog_hex64(frame_addr);
	klog_print(" frame_in_kernel=");
	klog_print(frame_in_kernel ? "1" : "0");
	klog_print(" frame_in_next_proc=");
	klog_print(frame_in_next_proc ? "1" : "0");
	klog_print(" frame_rip=");
	klog_hex64(frame ? frame->rip : 0);
	klog_print(" frame_rsp=");
	klog_hex64(frame ? frame->rsp : 0);
	klog_print(" frame_cs=");
	klog_hex64(next_proc ? task_get_cs(&next_proc->task) : 0);
	klog_print(" frame_ss=");
	klog_hex64(next_proc ? task_get_ss(&next_proc->task) : 0);
	klog_print(" frame_rflags=");
	klog_hex64(frame ? frame->rflags : 0);
	klog_print("\n");

	klog_print("CTX RESUME_FRAME rbx=");
	klog_hex64(frame ? frame->rbx : 0);
	klog_print(" rbp=");
	klog_hex64(frame ? frame->rbp : 0);
	klog_print(" r12=");
	klog_hex64(frame ? frame->r12 : 0);
	klog_print(" r13=");
	klog_hex64(frame ? frame->r13 : 0);
	klog_print(" r14=");
	klog_hex64(frame ? frame->r14 : 0);
	klog_print(" r15=");
	klog_hex64(frame ? frame->r15 : 0);
	klog_print(" rdi=");
	klog_hex64(frame ? frame->rdi : 0);
	klog_print(" rsi=");
	klog_hex64(frame ? frame->rsi : 0);
	klog_print(" rdx=");
	klog_hex64(frame ? frame->rdx : 0);
	klog_print(" r10=");
	klog_hex64(frame ? frame->r10 : 0);
	klog_print(" r8=");
	klog_hex64(frame ? frame->r8 : 0);
	klog_print(" r9=");
	klog_hex64(frame ? frame->r9 : 0);
	klog_print("\n");
#endif
}

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
