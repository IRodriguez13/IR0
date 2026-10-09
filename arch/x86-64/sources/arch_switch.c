/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_switch.c
 * Description: x86-64 switch_to implementation (TSS, syscall resume, iretq).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_switch.h>
#include <ir0/context.h>
#include <ir0/context_backend.h>
#include <ir0/task.h>
#include <ir0/arch_task.h>
#include <ir0/process.h>
#include <ir0/process_ctx_invariant.h>
#include <ir0/arch_port.h>
#include <ir0/ktm/deferred.h>
#include <ir0/arch_cpu.h>
#include <ir0/klog.h>
#include <ir0/debug_runtime.h>
#include <ir0/ktm/klog.h>
#include <ir0/oops.h>
#include <ir0/ktm/fault.h>
#include <config.h>
#include <ir0/paging.h>
#include <pmm.h>
#include <mm/allocator.h>

#include "switch_diag.h"

extern int switch_context_x64(task_t *prev, task_t *next);
extern uintptr_t paging_current_address_space(void);
extern uint64_t kernel_syscall_stack_top;
extern uint64_t user_rsp_save;
extern void tss_set_rsp0(uint64_t rsp0);


static int arch_va_in_kstack_window(uint64_t v)
{
	return v >= IR0_KSTACK_VA_BASE &&
	       v < IR0_KSTACK_VA_BASE +
		   (uint64_t)IR0_KSTACK_MAX_SLOTS * IR0_KSTACK_SLOT_SIZE;
}

/*
 * Pointer-class kernel VA (not a small syscall retval). Mid-syscall saves leave
 * kmalloc identity (SIMPLE_HEAP) in callee-saved regs; the old check only
 * caught the high kstack window, so heap RAX/RBP survived into ring-3
 * (desk re-login: read() "returned" ~0x1a8120 → SEGV in ir0_read_line).
 */
static int arch_va_kernel_ptr_leak(uint64_t v)
{
	if (v == 0)
		return 0;
	if (arch_va_in_kstack_window(v))
		return 1;
	if (v >= (uint64_t)SIMPLE_HEAP_START && v < (uint64_t)SIMPLE_HEAP_END)
		return 1;
	if (v >= 0xffff800000000000ULL)
		return 1;
	return 0;
}

/*
 * Ring-3 resume invariant: no GPR handed to user may be a kernel pointer.
 * A task saved mid-syscall keeps kernel callee-saved values; any path that
 * flips CS back to user without reapplying the entry frame would leak them.
 */
static int arch_task_user_gprs_leak(const task_t *t)
{
	return arch_va_kernel_ptr_leak(t->arch.rbp) ||
	       arch_va_kernel_ptr_leak(t->arch.rbx) ||
	       arch_va_kernel_ptr_leak(t->arch.r12) ||
	       arch_va_kernel_ptr_leak(t->arch.r13) ||
	       arch_va_kernel_ptr_leak(t->arch.r14) ||
	       arch_va_kernel_ptr_leak(t->arch.r15) ||
	       arch_va_kernel_ptr_leak(t->arch.rax);
}

/*
 * Reapply syscall_frame before user iretq when callee-saved GPRs still carry
 * kernel-stack residue.  Never run on kernel_syscall_sleep / want_kernel_ret:
 * those tasks resume via kernel_ret inside the syscall handler, not iretq with
 * task.arch GPRs (TTY read + SIGCHLD was spamming USER_RESUME_KSTACK_GPR_LEAK
 * and occasionally pushing a user frame onto a kernel_ret waiter → login #PF).
 */
static int arch_will_resume_user_iretq(const process_t *proc, const task_t *task)
{
	if (!task || !process_context_user_resume_eligible(proc))
		return 0;
	if (!task_cs_is_user(task))
		return 0;
	if (!process_rip_in_user_range(task_get_ip(task)))
		return 0;
	return 1;
}

static void arch_repair_user_gprs_from_syscall_frame(process_t *proc,
						     task_t *task)
{
	uint64_t rax;

	if (!proc || !task || proc->mode != USER_MODE)
		return;
	if (proc->kernel_syscall_sleep || proc->want_kernel_ret)
		return;
	if (!proc->syscall_frame_fresh || !task_cs_is_user(task))
		return;
	if (!arch_task_user_gprs_leak(task))
		return;

	rax = proc->syscall_resume_rax;
	if (rax == 0 && !arch_va_kernel_ptr_leak(task_get_retval(task)))
		rax = task_get_retval(task);
	klog_debug("CTX", "CLASSIFY USER_RESUME_KSTACK_GPR_LEAK");
	process_apply_syscall_frame_to_task(task, &proc->syscall_frame, rax);
}

void prepare_task_user_iretq(process_t *proc)
{
	if (!proc || proc->mode != USER_MODE)
		return;
	if (proc->kernel_syscall_sleep || proc->want_kernel_ret)
		return;
	arch_repair_user_gprs_from_syscall_frame(proc, &proc->task);
}

int context_backend_checkpoint(task_t *prev)
{
	if (!prev)
		return 0;
	return switch_context_x64(prev, NULL);
}

void context_backend_prepare_user_frame(struct process *proc, task_t *task)
{
	arch_repair_user_gprs_from_syscall_frame((process_t *)proc, task);
}

uint64_t context_backend_user_return_value(const struct process *proc,
					       const task_t *task)
{
	const process_t *resume_proc = (const process_t *)proc;
	uint64_t rax;

	if (!resume_proc)
		return 0;
	rax = resume_proc->syscall_resume_rax;
	if (rax == 0 && task && !arch_va_kernel_ptr_leak(task_get_retval(task)))
		rax = task_get_retval(task);
	return rax;
}

void set_current_kernel_stack(struct process *p)
{
	process_t *proc = (process_t *)p;

	if (!proc || !proc->kstack_top)
		return;

	kernel_syscall_stack_top = proc->kstack_top;
	tss_set_rsp0(proc->kstack_top);
	user_rsp_save = proc->saved_user_rsp;
}

void switch_save_user_rsp(struct process *prev)
{
	process_t *proc = (process_t *)prev;

	if (proc)
		proc->saved_user_rsp = user_rsp_save;
}

static void arch_fixup_user_task_for_iretq(process_t *proc)
{
	const syscall_user_frame_t *sf;
	uint64_t rip;

	if (!proc || proc->mode != USER_MODE)
		return;

	/*
	 * Blocked syscalls (TTY read, pipe, poll) resume in-kernel via
	 * kernel_ret — do not rewrite task.arch to the syscall entry frame.
	 */
	if (proc->kernel_syscall_sleep || proc->want_kernel_ret)
		return;

	/*
	 * wait4 blocked via process_arm_kernel_syscall_sleep: task_get_cs(task) is ring-0
	 * and resume must use switch_context_x64 kernel_ret into process_wait,
	 * not syscall_frame user iretq with placeholder rax=0.
	 */
	if (proc->wait_blocked && !proc->irq_frame_saved)
		return;

	if (proc->wait_target_pid != 0 && proc->wait_resume_child_pid <= 0 &&
	    !proc->irq_frame_saved)
		return;

	if ((task_get_cs(&proc->task) & 3u) == 0)
		return;

	rip = task_get_ip(&proc->task);
	if (rip >= 0x00400000ULL && rip <= 0x00007FFFFFFFFFFFULL)
		return;

	sf = &proc->syscall_frame;
	if (!process_rip_in_user_range(process_syscall_ip(proc)))
		return;

	process_apply_syscall_frame_to_task(&proc->task, sf, task_get_retval(&proc->task));
}

void arch_switch_to(task_t *prev, task_t *next)
{
    /*
     * High-regression area: wait4, irq_frame_saved, syscall_resume_rax,
     * and kernel_ret vs user-iret. Do not simplify these gates without
     * wait4 + blocked-syscall coverage.
     */
    process_t *prev_proc;
    process_t *next_proc = NULL;
    enum context_resume_route resume_route;

    if (next)
        next_proc = task_to_process(next);

    prev_proc = prev ? task_to_process(prev) : NULL;
    resume_route = context_prepare_resume_route(next);

    if (resume_route == CONTEXT_RESUME_USER_FRAME)
    {
		switch_audit_user_frame_resume(prev_proc, next_proc, next);
#if IR0_DEBUG_WAIT
        klog_info("WAIT", "CLASSIFY RESUME_GATE_USES_NEXT_FIXED");
        klog_debug("WAIT", "CTX resume_path=switch_to_user_task");
#endif
        if (next)
        {
			switch_trace_user_frame_resume(prev_proc, next_proc, next);
			(void)context_resume_user_frame(prev, next);
        }
        return;
    }

    arch_fixup_user_task_for_iretq(next_proc);

    context_finalize_kernel_resume(next);

    /*
     * KTM: force Class B on *next* (KERNEL CS + user RIP) before sanitize.
     * Seed syscall_frame so REPAIR can apply a coherent user iretq frame.
     * With IR0_CLASS_B_REPAIR=0 → KERNEL_RET_BAD_RIP.
     */
    if (next && next_proc && next_proc->mode == USER_MODE &&
        KTM_FAULT_HIT("sched.class_b_arm_window"))
    {
		task_set_kernel_segments(next);
		task_set_ip(next, IR0_USER_RIP_LO + 0x1000ULL);
        if (!process_rip_in_user_range(task_get_sp(next)))
            task_set_sp(next, 0x00007FFFFFF0ULL);
        process_syscall_set_ip(next_proc, task_get_ip(next));
        process_syscall_set_sp(next_proc, task_get_sp(next));
        process_syscall_set_flags(next_proc, task_get_flags(next) | 2ULL);
        klog_info("CTX", "CLASSIFY CLASS_B_FAULT_INJECT");
    }

    /*
     * Linux-like Class B safety net (IR0_CLASS_B_REPAIR): KERNEL_CS + user RIP
     * must not reach kernel_ret. Natural paths should not create this after
     * pt_regs-only capture + want_kernel_ret; KTM inject still exercises it.
     * Repair only when syscall_frame has usable user RIP/RSP.
     */
#if IR0_CLASS_B_REPAIR
	context_repair_kernel_return_state(next);
#endif

    /*
     * switch_context_x64 loads next CR3 while still on prev's RSP. Keep
     * shared kernel-half PDPT links (kstacks) fresh — asm bypasses
	 * paging_activate_address_space().
     */
    if (next)
    {
	uint64_t next_root = task_mm_root(next);

	if (next_root)
		paging_sync_kernel_mappings((address_space_root_t)next_root);
    }

    if (next_proc && next && arch_will_resume_user_iretq(next_proc, next))
        arch_repair_user_gprs_from_syscall_frame(next_proc, next);

    switch_context_x64(prev, next);
}
