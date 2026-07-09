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
#include <ir0/serial_io.h>
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
	serial_print("[WAIT_EXIT_AUDIT][CTX] prev_pid=");
	serial_print_hex32(prev_proc ? (uint32_t)prev_proc->task.pid : 0);
	serial_print(" prev_state=");
	serial_print_hex64(prev_proc ? (uint64_t)prev_proc->state : 0);
	serial_print(" prev_irq_saved=");
	serial_print_hex64(prev_proc ? (uint64_t)prev_proc->irq_frame_saved : 0);
	serial_print(" next_pid=");
	serial_print_hex32(next_proc ? (uint32_t)next_proc->task.pid : 0);
	serial_print(" next_state=");
	serial_print_hex64(next_proc ? (uint64_t)next_proc->state : 0);
	serial_print(" next_irq_saved=");
	serial_print_hex64(next_proc ? (uint64_t)next_proc->irq_frame_saved : 0);
	serial_print(" next_cr3=");
	serial_print_hex64(next ? next->cr3 : 0);
	serial_print(" active_cr3=");
	serial_print_hex64(get_current_page_directory());
	serial_print("\n");

	if (prev_proc && prev_proc->state == PROCESS_ZOMBIE)
	{
		serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] SCHED_SELECTED_ZOMBIE "
		             "note=prev_is_zombie_on_switch\n");
	}
	if (prev_proc && prev_proc->irq_frame_saved &&
	    (!next_proc || next_proc->state != PROCESS_BLOCKED))
	{
		serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] WAITPID_PARENT_CONTEXT_CORRUPT "
		             "reason=prev_irq_saved_but_next_not_blocked\n");
	}
	if (prev_proc && prev_proc->irq_frame_saved && next_proc &&
	    next_proc->irq_frame_saved == 0)
	{
		serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] WAITPID_PARENT_CONTEXT_CORRUPT "
		             "reason=resume_triggered_by_prev_irq_not_next\n");
	}

	if (next_proc)
	{
		uint64_t rip = next_proc->task.rip;
		uint64_t rsp = next_proc->task.rsp;
		uint16_t cs = next_proc->task.cs;
		uint16_t ss = next_proc->task.ss;

		serial_print("[WAIT_EXIT_AUDIT][CTX] next_user_frame rip=");
		serial_print_hex64(rip);
		serial_print(" rsp=");
		serial_print_hex64(rsp);
		serial_print(" cs=");
		serial_print_hex64((uint64_t)cs);
		serial_print(" ss=");
		serial_print_hex64((uint64_t)ss);
		serial_print(" rflags=");
		serial_print_hex64(next_proc->task.rflags);
		serial_print(" rax=");
		serial_print_hex64(next_proc->task.rax);
		serial_print("\n");

		if (next->cr3 == 0 && next_proc->page_directory)
		{
			serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] PARENT_CR3_BAD reason=task_cr3_zero\n");
		}
		if (rip < 0x00400000ULL || rip > 0x00007FFFFFFFFFFFULL)
		{
			serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] PARENT_IRET_FRAME_BAD_RIP\n");
		}
		if (rsp < 0x00400000ULL || rsp > 0x00007FFFFFFFFFFFULL)
		{
			serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] PARENT_IRET_FRAME_BAD_RSP\n");
		}
		if (cs != (uint16_t)USER_CODE_SEL || ss != (uint16_t)USER_DATA_SEL)
		{
			serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] PARENT_IRET_FRAME_BAD_CS_SS\n");
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
        if (next->cr3 == 0 && next_proc->page_directory)
            next->cr3 = (uint64_t)(uintptr_t)next_proc->page_directory;
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

#if CONFIG_DEBUG_FASE50
    serial_print("[FASE50][CTX] stage=arch_context_switch-entry prev=");
    serial_print_hex64((uint64_t)(uintptr_t)prev_proc);
    serial_print(" prev_pid=");
    serial_print_hex32(prev_proc ? (uint32_t)prev_proc->task.pid : 0);
    serial_print(" next=");
    serial_print_hex64((uint64_t)(uintptr_t)next_proc);
    serial_print(" next_pid=");
    serial_print_hex32(next_proc ? (uint32_t)next_proc->task.pid : 0);
    serial_print(" prev_state=");
    serial_print_hex64((uint64_t)(prev_proc ? prev_proc->state : 0));
    serial_print(" next_state=");
    serial_print_hex64((uint64_t)(next_proc ? next_proc->state : 0));
    serial_print(" active_cr3=");
    serial_print_hex64(get_current_page_directory());
    serial_print("\n");
#endif

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
        serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] RESUME_GATE_USES_NEXT_FIXED\n");
        serial_print("[WAIT_EXIT_AUDIT][CTX] resume_path=arch_switch_to_user_task\n");
#endif
        /*
         * Tear down the zombie child mm only after switching CR3 to the
         * waiting parent.  Reaping while the exiting child's CR3 is still
         * active unmaps the running page tables and faults mid-destroy.
         */
        if (next && next->cr3)
            load_page_directory(next->cr3);
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

            FASE40_D_AUDIT_LOG(
                serial_print("[FASE40_D_AUDIT][WAIT_RESUME] parent=");
                serial_print_hex32((uint32_t)next_proc->task.pid);
                serial_print(" target=");
                serial_print_hex32((uint32_t)next_proc->wait_target_pid);
                serial_print(" candidate=");
                serial_print_hex32((uint32_t)resume_child);
                serial_print("\n");
            );

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
                uint64_t active_cr3_after_expected = next ? next->cr3 : 0;
                uint64_t next_task_cr3 = next ? next->cr3 : 0;
                uint64_t current_before = (uint64_t)(uintptr_t)current_process;
                uint64_t frame_addr = (uint64_t)(uintptr_t)frame;
                uint64_t next_proc_addr = (uint64_t)(uintptr_t)next_proc;
                uint64_t next_proc_end = next_proc_addr + sizeof(process_t);
                int frame_in_next_proc = (frame_addr >= next_proc_addr &&
                                          frame_addr < next_proc_end);
                int frame_in_kernel = (frame_addr < 0x00400000ULL ||
                                       frame_addr > 0x00007FFFFFFFFFFFULL);

                serial_print("[CTX][RESUME] prev_pid=");
                serial_print_hex32(prev_proc ? (uint32_t)prev_proc->task.pid : 0);
                serial_print(" next_pid=");
                serial_print_hex32(next_proc ? (uint32_t)next_proc->task.pid : 0);
                serial_print(" current=");
                serial_print_hex64(current_before);
                serial_print(" active_cr3_before=");
                serial_print_hex64(active_cr3_before);
                serial_print(" active_cr3_pre_iret=");
                serial_print_hex64(get_current_page_directory());
                serial_print(" active_cr3_after_expected=");
                serial_print_hex64(active_cr3_after_expected);
                serial_print(" next_task_cr3=");
                serial_print_hex64(next_task_cr3);
                serial_print(" frame=");
                serial_print_hex64(frame_addr);
                serial_print(" frame_in_kernel=");
                serial_print(frame_in_kernel ? "1" : "0");
                serial_print(" frame_in_next_proc=");
                serial_print(frame_in_next_proc ? "1" : "0");
                serial_print(" frame_rip=");
                serial_print_hex64(frame ? frame->rip : 0);
                serial_print(" frame_rsp=");
                serial_print_hex64(frame ? frame->rsp : 0);
                serial_print(" frame_cs=");
                serial_print_hex64(next_proc ? next_proc->task.cs : 0);
                serial_print(" frame_ss=");
                serial_print_hex64(next_proc ? next_proc->task.ss : 0);
                serial_print(" frame_rflags=");
                serial_print_hex64(frame ? frame->rflags : 0);
                serial_print("\n");

                serial_print("[CTX][RESUME_FRAME] rbx=");
                serial_print_hex64(frame ? frame->rbx : 0);
                serial_print(" rbp=");
                serial_print_hex64(frame ? frame->rbp : 0);
                serial_print(" r12=");
                serial_print_hex64(frame ? frame->r12 : 0);
                serial_print(" r13=");
                serial_print_hex64(frame ? frame->r13 : 0);
                serial_print(" r14=");
                serial_print_hex64(frame ? frame->r14 : 0);
                serial_print(" r15=");
                serial_print_hex64(frame ? frame->r15 : 0);
                serial_print(" rdi=");
                serial_print_hex64(frame ? frame->rdi : 0);
                serial_print(" rsi=");
                serial_print_hex64(frame ? frame->rsi : 0);
                serial_print(" rdx=");
                serial_print_hex64(frame ? frame->rdx : 0);
                serial_print(" r10=");
                serial_print_hex64(frame ? frame->r10 : 0);
                serial_print(" r8=");
                serial_print_hex64(frame ? frame->r8 : 0);
                serial_print(" r9=");
                serial_print_hex64(frame ? frame->r9 : 0);
                serial_print("\n");
            }
#endif
#if CONFIG_DEBUG_FASE50
            serial_print("[FASE50][CTX] stage=arch_context_switch-irq_frame_resume pid=");
            serial_print_hex32(next_proc ? (uint32_t)next_proc->task.pid : 0);
            serial_print(" current_pre_iret=");
            serial_print_hex64((uint64_t)(uintptr_t)current_process);
            serial_print(" active_cr3_pre_iret=");
            serial_print_hex64(get_current_page_directory());
            serial_print("\n");
#endif
            arch_switch_to_user_task(next);
#if IR0_DEBUG_WAIT
            serial_print("[CTX][RESUME] unexpected_return active_cr3_after=");
            serial_print_hex64(get_current_page_directory());
            serial_print("\n");
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

#if CONFIG_DEBUG_FASE50
    serial_print("[FASE50][CTX] stage=arch_context_switch-before-switch_context_x64\n");
#endif
    switch_context_x64(prev, next);
#endif
}

uint64_t arch_get_current_page_directory(void)
{
    return get_current_page_directory();
}
