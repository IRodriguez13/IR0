/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_context_switch.c
 * Description: Architecture-dispatched context switch wrappers (ISA stubs are private).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/task.h>
#include <ir0/process.h>
#include <ir0/arch_port.h>
#include <ir0/klog.h>
#include <ir0/debug_runtime.h>
#include <config.h>
#include <ir0/paging.h>
#include <pmm.h>

#if defined(ARCH_ARM64)
extern void switch_context_arm64(task_t *prev, task_t *next);
#else
extern void switch_context_x64(task_t *prev, task_t *next);
#endif

extern uint64_t get_current_page_directory(void);

#if !defined(ARCH_ARM64)
/* asm globals: arch/x86-64/asm/syscall_insn_entry_64.asm */
extern uint64_t kernel_syscall_stack_top; /* .data: top of syscall-insn stack */
extern uint64_t user_rsp_save;            /* .bss: user RSP scratch for sysret */
extern void tss_set_rsp0(uint64_t rsp0);

void arch_set_current_kernel_stack(process_t *p)
{
    if (!p || !p->kstack_top)
        return;

    /* Future syscall-insn entry of this task lands on its private stack. */
    kernel_syscall_stack_top = p->kstack_top;
    /* Future CPL3->CPL0 trap (int 0x80 / IRQ from user) likewise. */
    tss_set_rsp0(p->kstack_top);
    /*
     * Restore this task's user-RSP shadow so an in-kernel block loop resumed
     * via kernel_ret restores its own user RSP at sysret, not a peer's.
     */
    user_rsp_save = p->saved_user_rsp;
}
#else
void arch_set_current_kernel_stack(process_t *p)
{
    (void)p;
}
#endif

static void arch_fixup_user_task_for_iretq(process_t *proc)
{
	const syscall_user_frame_t *sf;
	uint64_t rip;

	if (!proc || proc->mode != USER_MODE)
		return;

	/*
	 * wait4 blocked via process_arm_kernel_syscall_sleep: task->cs is ring-0
	 * and resume must use switch_context_x64 kernel_ret into process_wait,
	 * not syscall_frame user iretq with placeholder rax=0.
	 */
	if (proc->wait_blocked && !proc->irq_frame_saved)
		return;

	if (proc->wait_target_pid != 0 && proc->wait_resume_child_pid <= 0 &&
	    !proc->irq_frame_saved)
		return;

	if ((proc->task.cs & 3u) == 0)
		return;

	rip = proc->task.rip;
	if (rip >= 0x00400000ULL && rip <= 0x00007FFFFFFFFFFFULL)
		return;

	sf = &proc->syscall_frame;
	if (sf->rip < 0x00400000ULL || sf->rip > 0x00007FFFFFFFFFFFULL)
		return;

	process_apply_syscall_frame_to_task(&proc->task, sf, proc->task.rax);
}

static void wait_exit_audit_ctx_resume(process_t *prev_proc, process_t *next_proc,
                                       task_t *next)
{
#if !IR0_DEBUG_WAIT
	(void)prev_proc;
	(void)next_proc;
	(void)next;
	return;
#else
	klog_print("[WAIT_EXIT_AUDIT][CTX] prev_pid=");
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
	klog_hex64(get_current_page_directory());
	klog_print("\n");

	if (prev_proc && prev_proc->state == PROCESS_ZOMBIE)
	{
		klog_print("[WAIT_EXIT_AUDIT][CLASSIFY] SCHED_SELECTED_ZOMBIE "
		             "note=prev_is_zombie_on_switch\n");
	}
	if (prev_proc && prev_proc->irq_frame_saved &&
	    (!next_proc || next_proc->state != PROCESS_BLOCKED))
	{
		klog_print("[WAIT_EXIT_AUDIT][CLASSIFY] WAITPID_PARENT_CONTEXT_CORRUPT "
		             "reason=prev_irq_saved_but_next_not_blocked\n");
	}
	if (prev_proc && prev_proc->irq_frame_saved && next_proc &&
	    next_proc->irq_frame_saved == 0)
	{
		klog_print("[WAIT_EXIT_AUDIT][CLASSIFY] WAITPID_PARENT_CONTEXT_CORRUPT "
		             "reason=resume_triggered_by_prev_irq_not_next\n");
	}

	if (next_proc)
	{
		uint64_t rip = next_proc->task.rip;
		uint64_t rsp = next_proc->task.rsp;
		uint16_t cs = next_proc->task.cs;
		uint16_t ss = next_proc->task.ss;

		klog_print("[WAIT_EXIT_AUDIT][CTX] next_user_frame rip=");
		klog_hex64(rip);
		klog_print(" rsp=");
		klog_hex64(rsp);
		klog_print(" cs=");
		klog_hex64((uint64_t)cs);
		klog_print(" ss=");
		klog_hex64((uint64_t)ss);
		klog_print(" rflags=");
		klog_hex64(next_proc->task.rflags);
		klog_print(" rax=");
		klog_hex64(next_proc->task.rax);
		klog_print("\n");

		if (task_mm_root(next) == 0 && next_proc->page_directory)
		{
			klog_print("[WAIT_EXIT_AUDIT][CLASSIFY] PARENT_CR3_BAD reason=task_cr3_zero\n");
		}
		if (rip < 0x00400000ULL || rip > 0x00007FFFFFFFFFFFULL)
		{
			klog_print("[WAIT_EXIT_AUDIT][CLASSIFY] PARENT_IRET_FRAME_BAD_RIP\n");
		}
		if (rsp < 0x00400000ULL || rsp > 0x00007FFFFFFFFFFFULL)
		{
			klog_print("[WAIT_EXIT_AUDIT][CLASSIFY] PARENT_IRET_FRAME_BAD_RSP\n");
		}
		if (cs != (uint16_t)USER_CODE_SEL || ss != (uint16_t)USER_DATA_SEL)
		{
			klog_print("[WAIT_EXIT_AUDIT][CLASSIFY] PARENT_IRET_FRAME_BAD_CS_SS\n");
		}
	}
#endif
}

void arch_context_switch(task_t *prev, task_t *next)
{
#if defined(ARCH_ARM64)
    switch_context_arm64(prev, next);
#else
    process_t *prev_proc;
    process_t *next_proc = NULL;

    if (next)
    {
        next_proc = task_to_process(next);
        if (task_mm_root(next) == 0 && next_proc->page_directory)
            task_set_mm_root(next, (uint64_t)(uintptr_t)next_proc->page_directory);
    }

    prev_proc = prev ? task_to_process(prev) : NULL;

    /*
     * Per-process kernel stack handoff. Snapshot the outgoing task's live user
     * RSP shadow, then point the kernel entry stacks (+ user RSP shadow) at the
     * incoming task. Covers all resume paths below (arch_switch_to_user_task,
     * kernel_ret, user iretq) since every one funnels through here.
     */
    if (prev_proc)
        prev_proc->saved_user_rsp = user_rsp_save;
    arch_set_current_kernel_stack(next_proc);

    /*
     * wait4 in progress without a staged child pid: force kernel resume.
     * Preserve irq_frame_saved when wait_blocked (syscall_frame sleep) so
     * child-exit wake can stage wait_resume_child_pid before user iret.
     */
    if (next_proc && next_proc->mode == USER_MODE &&
        next_proc->wait_resume_child_pid <= 0 &&
        (next_proc->wait_blocked || next_proc->wait_target_pid != 0) &&
        !next_proc->coop_resched_resume)
    {
        process_arm_kernel_syscall_sleep(next_proc);
        if (!next_proc->wait_blocked)
        {
            next_proc->irq_frame_saved = 0;
            next_proc->coop_resched_resume = 0;
        }
    }


    /*
     * Syscall-block resume (wait4): resume the task we are switching TO when
     * it blocked with a saved user frame.  Never key off prev->irq_frame_saved
     * (stale timer IRQ flags on exiting/zombie tasks misroute resume).
     */
    if (next_proc && next_proc->irq_frame_saved)
    {
        const int wait_sleep_no_child =
            (next_proc->wait_blocked || next_proc->wait_target_pid != 0) &&
            next_proc->wait_resume_child_pid <= 0 &&
            !next_proc->coop_resched_resume;

        /*
         * wait4 blocked with no reaped child yet — kernel_ret into process_wait,
         * never user-iret with placeholder syscall_resume_rax=0. Keep
         * irq_frame_saved when wait_blocked so wake can stage the child pid.
         */
        if (wait_sleep_no_child)
        {
            process_arm_kernel_syscall_sleep(next_proc);
        }
        else if (next_proc->syscall_resume_rax == 0 &&
                 !(next_proc->wait_blocked &&
                   next_proc->wait_resume_child_pid > 0))
        {
            /*
             * Stale syscall-frame resume (pipe-read block placeholder, coop
             * reschedule with rax=0, etc.). Continue in kernel instead of
             * iretq with rax=0.
             */
            next_proc->irq_frame_saved = 0;
            next_proc->coop_resched_resume = 0;
        }
        else
        {
        syscall_user_frame_t *frame = &next_proc->syscall_frame;

        wait_exit_audit_ctx_resume(prev_proc, next_proc, next);
#if IR0_DEBUG_WAIT
        klog_print("[WAIT_EXIT_AUDIT][CLASSIFY] RESUME_GATE_USES_NEXT_FIXED\n");
        klog_print("[WAIT_EXIT_AUDIT][CTX] resume_path=arch_switch_to_user_task\n");
#endif
        /*
         * Tear down the zombie child mm only after switching CR3 to the
         * waiting parent.  Reaping while the exiting child's CR3 is still
         * active unmaps the running page tables and faults mid-destroy.
         */
        if (next && task_mm_root(next))
            load_page_directory(task_mm_root(next));
        /*
         * Cooperative reschedule resume carries a syscall retval in
         * syscall_resume_rax, not a child pid — skip the wait4 zombie reap so
         * a retval that happens to match a zombie pid cannot reap it.
         */
        if (!next_proc->coop_resched_resume)
        {
            pid_t resume_child = next_proc->wait_resume_child_pid;

            if (resume_child <= 0)
                resume_child = (pid_t)next_proc->syscall_resume_rax;


            process_reap_zombie_on_wait_resume(next_proc, resume_child);
        }
        {
            uint64_t resume_rax = next_proc->syscall_resume_rax;

            if (!next_proc->coop_resched_resume && next_proc->wait_blocked &&
                next_proc->wait_resume_child_pid > 0)
                resume_rax = (uint64_t)next_proc->wait_resume_child_pid;

            process_apply_syscall_frame_to_task(&next_proc->task, frame,
                                                resume_rax);
        }
        next_proc->wait_status_ptr = NULL;
        next_proc->wait_blocked = 0;
        next_proc->wait_target_pid = 0;
        next_proc->wait_options = 0;
        next_proc->wait_resume_child_pid = 0;
        next_proc->irq_frame_saved = 0;
        next_proc->coop_resched_resume = 0;
        if (next)
        {
#if IR0_DEBUG_WAIT
            {
                uint64_t active_cr3_before = get_current_page_directory();
                uint64_t active_cr3_after_expected = next ? task_mm_root(next) : 0;
                uint64_t next_task_cr3 = next ? task_mm_root(next) : 0;
                uint64_t current_before = (uint64_t)(uintptr_t)current_process;
                uint64_t frame_addr = (uint64_t)(uintptr_t)frame;
                uint64_t next_proc_addr = (uint64_t)(uintptr_t)next_proc;
                uint64_t next_proc_end = next_proc_addr + sizeof(process_t);
                int frame_in_next_proc = (frame_addr >= next_proc_addr &&
                                          frame_addr < next_proc_end);
                int frame_in_kernel = (frame_addr < 0x00400000ULL ||
                                       frame_addr > 0x00007FFFFFFFFFFFULL);

                klog_print("[CTX][RESUME] prev_pid=");
                klog_hex32(prev_proc ? (uint32_t)prev_proc->task.pid : 0);
                klog_print(" next_pid=");
                klog_hex32(next_proc ? (uint32_t)next_proc->task.pid : 0);
                klog_print(" current=");
                klog_hex64(current_before);
                klog_print(" active_cr3_before=");
                klog_hex64(active_cr3_before);
                klog_print(" active_cr3_pre_iret=");
                klog_hex64(get_current_page_directory());
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
                klog_hex64(next_proc ? next_proc->task.cs : 0);
                klog_print(" frame_ss=");
                klog_hex64(next_proc ? next_proc->task.ss : 0);
                klog_print(" frame_rflags=");
                klog_hex64(frame ? frame->rflags : 0);
                klog_print("\n");

                klog_print("[CTX][RESUME_FRAME] rbx=");
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
            }
#endif
            arch_switch_to_user_task(next);
#if IR0_DEBUG_WAIT
            klog_print("[CTX][RESUME] unexpected_return active_cr3_after=");
            klog_hex64(get_current_page_directory());
            klog_print("\n");
#endif
        }
        return;
        }
    }

    /*
     * wait4 blocked in process_wait: re-assert ring-0 before switch_context.
     */
    if (next_proc && next_proc->wait_target_pid != 0 &&
        next_proc->wait_resume_child_pid <= 0 && !next_proc->coop_resched_resume)
    {
        process_arm_kernel_syscall_sleep(next_proc);
    }

    arch_fixup_user_task_for_iretq(next_proc);

    switch_context_x64(prev, next);
#endif
}

uint64_t arch_get_current_page_directory(void)
{
    return get_current_page_directory();
}
