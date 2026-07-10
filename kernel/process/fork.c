/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fork.c
 * Description: POSIX fork, deferred enqueue, rollback, and fork-return diagnostics.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"

#if defined(__x86_64__) || defined(__amd64__)
/*
 * fork_ret - register-level fork child return audit (PRE_RETURN + FIRST_ENTRY).
 */
typedef struct
{
	uint64_t rax;
	uint64_t rcx;
	uint64_t r11;
	uint64_t rbx;
	uint64_t rbp;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;
	uint64_t rsp;
	uint64_t rip;
} fork_ret_pre_regs_t;

fork_ret_pre_regs_t fork_ret_pre_regs;

/*
 * fork_restore_audit — GPR restore path before iretq (ASM + C).
 * Layout must match switch_x64.asm offsets (FORK_RESTORE_AUDIT_*).
 */
typedef struct
{
	uint64_t magic;
	uint64_t task_ptr;
	uint64_t rax_slot_addr;
	uint64_t rax_slot_mem;
	uint64_t rsp_pre_gpr_load;
	uint64_t stack_qwords[20];
	uint64_t restore_method;
	uint64_t stack_rax_slot_off;
	uint64_t live_rax_after_task_load;
	uint64_t live_rbx_after_task_load;
	uint64_t live_rax_after_pr_call;
	uint64_t live_rax_pre_iretq;
	uint64_t live_rbx_pre_iretq;
	uint64_t live_rcx_pre_iretq;
	uint64_t live_rdx_pre_iretq;
	uint64_t kernel_rsp_pre_iretq;
	uint64_t iretq_rip;
	uint64_t iretq_rflags;
	uint64_t iretq_user_rsp;
	uint64_t task_rax_at_fixup;
	uint64_t pre_return_log_rax;
	uint64_t userspace_rax;
	uint64_t classify_emit;
} fork_restore_audit_t;

fork_restore_audit_t fork_restore_audit;

extern uint8_t fork_flow_set_tf;

static void fork_restore_dump_qwords(const char *tag, uint64_t base, size_t n)
{
	size_t i;

	if (!DEBUG_FORK)
		return;
	serial_print("[FORK_RESTORE] ");
	serial_print(tag ? tag : "frame");
	serial_print(" base=");
	serial_print_hex64(base);
	serial_print(" qwords=");
	for (i = 0; i < n; i++)
	{
		uint64_t addr = base + (uint64_t)(i * sizeof(uint64_t));
		uint64_t v = *(const uint64_t *)(uintptr_t)addr;

		if (i > 0)
			serial_print(" ");
		serial_print_hex64(v);
	}
	serial_print("\n");
}

static void fork_restore_classify(uint64_t userspace_rax)
{
	const char *tag = NULL;
	uint64_t slot;
	uint64_t after_load;
	uint64_t pre_iret;
	uint64_t pre_log;
	uint64_t fixup_rax;

	if (fork_restore_audit.classify_emit)
		return;

	fork_restore_audit.classify_emit = 1;
	fork_restore_audit.userspace_rax = userspace_rax;
	slot = fork_restore_audit.rax_slot_mem;
	after_load = fork_restore_audit.live_rax_after_task_load;
	pre_iret = fork_restore_audit.live_rax_pre_iretq;
	pre_log = fork_restore_audit.pre_return_log_rax;
	fixup_rax = fork_restore_audit.task_rax_at_fixup;

	if (fixup_rax == 0 && slot != 0)
		tag = "RAX_SLOT_STALE";
	else if (after_load != slot)
		tag = "RESTORE_SOURCE_MISMATCH";
	else if (pre_log != after_load)
		tag = "PRE_RETURN_LOG_NOT_AUTHORITATIVE";
	else if (after_load == 0 && pre_iret != 0)
		tag = "LATE_RAX_CLOBBER";
	else if (after_load == 0 && pre_iret == 0 && fixup_rax == 0)
		tag = "LATE_RAX_CLOBBER_FIXED";

	if (!tag)
		return;

	if (!DEBUG_FORK)
		return;
	serial_print("[FORK_RESTORE][CLASSIFY] ");
	serial_print(tag);
	serial_print("\n");
}

static void fork_restore_log_fixup(process_t *parent, process_t *child)
{
	uint64_t rax_before;

	rax_before = child->task.rax;
	fork_restore_audit.magic = 0xF010CAFEULL;
	fork_restore_audit.task_ptr = (uint64_t)(uintptr_t)&child->task;
	fork_restore_audit.rax_slot_addr =
		(uint64_t)(uintptr_t)&child->task.rax;

	if (DEBUG_FORK)
	{
		serial_print("[FORK_RESTORE][FIXUP] child_task=");
		serial_print_hex64(fork_restore_audit.task_ptr);
		serial_print(" syscall_frame=");
		serial_print_hex64((uint64_t)(uintptr_t)&parent->syscall_frame);
		serial_print(" rax_slot_addr=");
		serial_print_hex64(fork_restore_audit.rax_slot_addr);
		serial_print(" task_rax_before=");
		serial_print_hex64(rax_before);
	}

	process_apply_syscall_frame_to_task(&child->task, &parent->syscall_frame, 0);
	process_apply_syscall_frame_to_task(&parent->task, &parent->syscall_frame,
	                                    (uint64_t)child->task.pid);

	fork_restore_audit.task_rax_at_fixup = child->task.rax;
	fork_restore_audit.rax_slot_mem = child->task.rax;

	if (DEBUG_FORK)
	{
		serial_print(" task_rax_after=");
		serial_print_hex64(child->task.rax);
		serial_print(" slot_readback=");
		serial_print_hex64(*(uint64_t *)(uintptr_t)fork_restore_audit.rax_slot_addr);
		serial_print("\n");
	}
}

#define FORK_BRANCH_RIP_MOV   0x402BA3ULL
#define FORK_BRANCH_RIP_TEST  0x402BA6ULL
#define FORK_BRANCH_RIP_JE    0x402BA8ULL
#define FORK_BRANCH_RIP_CHILD 0x402BE8ULL
#define FORK_BRANCH_RIP_PARENT 0x402BAAULL

static void fork_flow_hex_byte(uint8_t b)
{
	static const char *digits = "0123456789abcdef";
	char out[3];

	out[0] = digits[(b >> 4) & 0x0FU];
	out[1] = digits[b & 0x0FU];
	out[2] = '\0';
	serial_print(out);
}

static int fork_flow_read_user_bytes(process_t *proc, uint64_t va, uint8_t *buf,
                                     size_t n)
{
	size_t i;

	if (!proc || !proc->page_directory || !buf || n == 0)
		return -1;

	for (i = 0; i < n; i++)
	{
		uintptr_t page = (uintptr_t)((va + i) & ~0xFFFULL);
		size_t off = (size_t)((va + i) & 0xFFFULL);
		uint64_t *pte = paging_get_pte(proc->page_directory, page);
		uintptr_t phys;

		if (!pte || !(*pte & PAGE_PRESENT))
			return -1;

		phys = (uintptr_t)(*pte & PAGE_PTE_PFN_MASK);
		buf[i] = *(const uint8_t *)(phys + off);
	}

	return 0;
}

static struct
{
	uint8_t active;
	uint8_t pre_return_done;
	pid_t child_pid;
	uint64_t expected_rax;
	uint64_t expected_rip;
	uint64_t expected_rsp;
	uint64_t task_rax_at_fixup;
	uint64_t pre_rax;
	uint64_t pre_rip;
	uint64_t pre_rsp;
} fork_ret_expect;

static struct
{
	uint8_t active;
	uint8_t step;
	uint8_t classified;
	uint8_t code_dumped;
	pid_t child_pid;
	uint64_t step_rax[3];
	uint64_t step_rbx[3];
	uint64_t step_rip[3];
	uint64_t step_rflags[3];
	uint64_t step_rsp[3];
	uint64_t step_zf[3];
} fork_branch;

static void fork_branch_hex8(process_t *proc, uint64_t va, const char *tag)
{
	uint8_t bytes[8];
	size_t i;

	if (fork_flow_read_user_bytes(proc, va, bytes, sizeof(bytes)) != 0)
	{
		serial_print("[FORK_BRANCH] ");
		serial_print(tag ? tag : "code");
		serial_print(" unreadable\n");
		return;
	}

	serial_print("[FORK_BRANCH] ");
	serial_print(tag ? tag : "code");
	serial_print("=");
	for (i = 0; i < sizeof(bytes); i++)
	{
		if (i > 0)
			serial_print(" ");
		fork_flow_hex_byte(bytes[i]);
	}
	serial_print("\n");
}

static void fork_branch_emit_step(process_t *proc, const char *label, uint32_t idx,
                                  uint64_t rip, uint64_t rax, uint64_t rbx,
                                  uint64_t rflags, uint64_t rsp)
{
	uint64_t zf;

	zf = (rflags >> 6) & 1ULL;
	serial_print("[FORK_BRANCH] step=");
	serial_print(label ? label : "?");
	serial_print(" rip=");
	serial_print_hex64(rip);
	serial_print(" rax=");
	serial_print_hex64(rax);
	serial_print(" rbx=");
	serial_print_hex64(rbx);
	serial_print(" rflags=");
	serial_print_hex64(rflags);
	serial_print(" zf=");
	serial_print_hex64(zf);
	serial_print(" rsp=");
	serial_print_hex64(rsp);
	serial_print("\n");

	if (idx < 3)
	{
		fork_branch.step_rax[idx] = rax;
		fork_branch.step_rbx[idx] = rbx;
		fork_branch.step_rip[idx] = rip;
		fork_branch.step_rflags[idx] = rflags;
		fork_branch.step_rsp[idx] = rsp;
		fork_branch.step_zf[idx] = zf;
	}

	if (idx == 0 && fork_ret_expect.active)
	{
		fork_restore_emit_pre_iretq();
		fork_restore_classify(rax);
	}

	if (!fork_branch.code_dumped && proc)
	{
		fork_branch_hex8(proc, FORK_BRANCH_RIP_MOV, "code_at_402BA3");
		fork_branch_hex8(proc, FORK_BRANCH_RIP_TEST, "code_at_402BA6");
		fork_branch_hex8(proc, FORK_BRANCH_RIP_JE, "code_at_402BA8");
		fork_branch.code_dumped = 1;
	}
}

static void fork_branch_classify(void)
{
	const char *tag;
	uint64_t rax0;
	uint64_t rax1;
	uint64_t zf1;
	uint64_t rip2;

	if (fork_branch.classified)
		return;

	fork_branch.classified = 1;
	fork_flow_set_tf = 0;
	rax0 = fork_branch.step_rax[0];
	rax1 = fork_branch.step_rax[1];
	zf1 = fork_branch.step_zf[1];
	rip2 = fork_branch.step_rip[2];

	if (rax0 != rax1)
		tag = "RAX_MUTATED_BETWEEN_STEPS";
	else if (((rax1 == 0) ? 1ULL : 0ULL) != zf1)
		tag = "FLAGS_UNEXPECTED";
	else if (rax1 == 0 && rip2 != FORK_BRANCH_RIP_CHILD)
		tag = "BRANCH_PARENT_WITH_RAX_ZERO";
	else if (rip2 == FORK_BRANCH_RIP_CHILD)
		tag = "BRANCH_CHILD_OK";
	else
		return;

	serial_print("[FORK_BRANCH][CLASSIFY] ");
	serial_print(tag);
	serial_print("\n");
}

static void fork_branch_arm_pid(pid_t child_pid)
{
	memset(&fork_branch, 0, sizeof(fork_branch));
	fork_branch.active = 1;
	fork_branch.child_pid = child_pid;
}

int fork_flow_note_debug_exception(uint64_t *stack)
{
	process_t *p = current_process;
	uint64_t rip;
	uint64_t rax;
	uint64_t rbx;
	uint64_t rflags;
	uint64_t rsp;
	const char *label;

	if (!fork_branch.active || fork_branch.classified || !stack || !p ||
	    p->task.pid != fork_branch.child_pid)
		return 0;

	rip = stack[2];
	rflags = stack[4];
	rsp = stack[5];
	rax = stack[-1];
	rbx = stack[-4];

	if (fork_branch.step == 0)
		label = "STEP0";
	else if (fork_branch.step == 1)
		label = "STEP1";
	else if (fork_branch.step == 2)
		label = "STEP2";
	else
	{
		stack[4] &= ~0x100ULL;
		fork_flow_set_tf = 0;
		return 0;
	}

	fork_branch_emit_step(p, label, fork_branch.step, rip, rax, rbx, rflags, rsp);
	fork_branch.step++;

	if (fork_branch.step >= 3)
	{
		stack[4] &= ~0x100ULL;
		fork_branch_classify();
	}
	else
		stack[4] |= 0x100ULL;

	return 1;
}

void fork_flow_note_kernel_entry(uint64_t rip_hw, uint64_t nr, int from_syscall)
{
	(void)rip_hw;
	(void)nr;
	(void)from_syscall;
}

void fork_ret_emit_pre_return(void)
{
	process_t *p = current_process;
	const fork_ret_pre_regs_t *pre = &fork_ret_pre_regs;

	if (!fork_ret_expect.active || fork_ret_expect.pre_return_done)
		return;
	if (!p || p->task.pid != fork_ret_expect.child_pid)
		return;

	fork_ret_expect.pre_return_done = 1;
	fork_ret_expect.pre_rax = pre->rax;
	fork_ret_expect.pre_rip = pre->rip;
	fork_ret_expect.pre_rsp = pre->rsp;
	if (ir0_debug_fork_singlestep_active())
		fork_flow_set_tf = 1;
	fork_restore_audit.pre_return_log_rax = pre->rax;

	if (!DEBUG_FORK)
		return;

	serial_print("[FORK_RET][PRE_RETURN] pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" task_ptr=");
	serial_print_hex64((uint64_t)(uintptr_t)&p->task);
	serial_print(" rax_slot_addr=");
	serial_print_hex64((uint64_t)(uintptr_t)&p->task.rax);
	serial_print(" rax_slot_val=");
	serial_print_hex64(p->task.rax);
	serial_print(" restore_method=");
	serial_print_hex64(fork_restore_audit.restore_method);
	serial_print(" rax=");
	serial_print_hex64(pre->rax);
	serial_print(" live_after_task_load=");
	serial_print_hex64(fork_restore_audit.live_rax_after_task_load);
	serial_print(" rcx=");
	serial_print_hex64(pre->rcx);
	serial_print(" r11=");
	serial_print_hex64(pre->r11);
	serial_print(" rbx=");
	serial_print_hex64(pre->rbx);
	serial_print(" rbp=");
	serial_print_hex64(pre->rbp);
	serial_print(" r12=");
	serial_print_hex64(pre->r12);
	serial_print(" r13=");
	serial_print_hex64(pre->r13);
	serial_print(" r14=");
	serial_print_hex64(pre->r14);
	serial_print(" r15=");
	serial_print_hex64(pre->r15);
	serial_print(" rsp=");
	serial_print_hex64(pre->rsp);
	serial_print(" rip=");
	serial_print_hex64(pre->rip);
	serial_print(" task_rax=");
	serial_print_hex64(p->task.rax);
	serial_print("\n");

	fork_restore_dump_qwords("stack_at_pre_return", pre->rsp, 20);
	fork_restore_dump_qwords("asm_stack_pre_gpr", fork_restore_audit.rsp_pre_gpr_load, 20);
	serial_print("[FORK_RESTORE] asm_rax_slot_mem=");
	serial_print_hex64(fork_restore_audit.rax_slot_mem);
	serial_print(" stack_rax_off=");
	serial_print_hex64(fork_restore_audit.stack_rax_slot_off);
	serial_print(" live_after_pr_call=");
	serial_print_hex64(fork_restore_audit.live_rax_after_pr_call);
	serial_print("\n");
}

void fork_restore_emit_pre_iretq(void)
{
	if (!fork_ret_expect.active || !fork_ret_expect.pre_return_done)
		return;
	if (!DEBUG_FORK)
		return;

	serial_print("[FORK_RESTORE][PRE_IRETQ] live_rax=");
	serial_print_hex64(fork_restore_audit.live_rax_pre_iretq);
	serial_print(" live_rbx=");
	serial_print_hex64(fork_restore_audit.live_rbx_pre_iretq);
	serial_print(" live_rcx=");
	serial_print_hex64(fork_restore_audit.live_rcx_pre_iretq);
	serial_print(" live_rdx=");
	serial_print_hex64(fork_restore_audit.live_rdx_pre_iretq);
	serial_print(" kernel_rsp=");
	serial_print_hex64(fork_restore_audit.kernel_rsp_pre_iretq);
	serial_print(" iretq_rip=");
	serial_print_hex64(fork_restore_audit.iretq_rip);
	serial_print(" iretq_rflags=");
	serial_print_hex64(fork_restore_audit.iretq_rflags);
	serial_print(" iretq_user_rsp=");
	serial_print_hex64(fork_restore_audit.iretq_user_rsp);
	serial_print("\n");
}

void fork_ret_first_syscall_entry(uint64_t rax_hw, uint64_t rip_hw, uint64_t rsp_hw)
{
	process_t *p = current_process;

	if (!fork_ret_expect.active || !p || p->task.pid != fork_ret_expect.child_pid)
		return;
	if (!fork_ret_expect.pre_return_done)
		return;

	if (DEBUG_FORK && !fork_branch.classified)
	{
		serial_print("[FORK_RET][FIRST_ENTRY] pid=");
		serial_print_hex32((uint32_t)p->task.pid);
		serial_print(" rax=");
		serial_print_hex64(rax_hw);
		serial_print(" rip=");
		serial_print_hex64(rip_hw);
		serial_print(" rsp=");
		serial_print_hex64(rsp_hw);
		serial_print("\n");
	}
}

static void fork_ret_arm(process_t *child)
{
	memset(&fork_ret_expect, 0, sizeof(fork_ret_expect));
	fork_ret_expect.active = 1;
	fork_ret_expect.child_pid = child->task.pid;
	fork_ret_expect.expected_rax = child->task.rax;
	fork_ret_expect.expected_rip = child->task.rip;
	fork_ret_expect.expected_rsp = child->task.rsp;
	fork_ret_expect.task_rax_at_fixup = child->task.rax;
	fork_branch_arm_pid(child->task.pid);
}

static void fork_fixup_user_syscall_return(process_t *parent, process_t *child)
{
	memset(&fork_restore_audit, 0, sizeof(fork_restore_audit));
	fork_restore_log_fixup(parent, child);
	fork_ret_arm(child);
	/* Parent RESTORE_ALL uses syscall_frame captured at fork syscall entry. */
}

#endif /* __x86_64__ */

#if !(defined(__x86_64__) || defined(__amd64__))
void fork_ret_emit_pre_return(void)
{
}

void fork_restore_emit_pre_iretq(void)
{
}

void fork_ret_first_syscall_entry(uint64_t rax_hw, uint64_t rip_hw, uint64_t rsp_hw)
{
	(void)rax_hw;
	(void)rip_hw;
	(void)rsp_hw;
}

int fork_flow_note_debug_exception(uint64_t *stack)
{
	(void)stack;
	return 0;
}

void fork_flow_note_kernel_entry(uint64_t rip_hw, uint64_t nr, int from_syscall)
{
	(void)rip_hw;
	(void)nr;
	(void)from_syscall;
}
#endif

static process_t *fork_process_create(process_t *parent, pid_t *child_pid_out)
{
	process_t *child;
	pid_t child_pid;

	if (!parent || !child_pid_out)
		return NULL;

	child = kmalloc_try(sizeof(process_t));
	if (!child)
		return NULL;

	memcpy(child, parent, sizeof(process_t));

	child_pid = process_get_next_pid();
	child->task.pid = child_pid;
	child->tgid = child_pid;
	child->ppid = parent->task.pid;
	child->state = PROCESS_READY;
	child->next = NULL;
	child->saved_context = NULL;
	child->poll_waiter = NULL;
	child->poll_resume_via_arch = 0;
	child->fork_pending_child = NULL;
	child->fork_resync_syscall_stack = 0;
	child->irq_frame_saved = 0;
	child->coop_resched_resume = 0;
	child->syscall_frame_fresh = 0;
	child->wait_blocked = 0;
	child->wait_target_pid = 0;
	child->wait_options = 0;
	child->wait_resume_child_pid = 0;
	child->wait_status_ptr = NULL;
	child->syscall_resume_rax = 0;
#if IR0_DEBUG_PROC
	fase_audit_fork_init(child, parent);
#endif
	child->page_directory = NULL;
	child->owns_page_directory = 0;
	child->mmap_list = NULL;
	memset(child->fd_table, 0, sizeof(child->fd_table));

	/*
	 * memcpy copied the parent's kernel-stack pointer; the child needs its own
	 * private kernel stack (a shared one would corrupt both on concurrent
	 * syscalls). Reset before allocating so a failure cannot free parent's.
	 */
	child->kstack_base = NULL;
	child->kstack_top = 0;
	child->saved_user_rsp = 0;
	if (process_kernel_stack_alloc(child) != 0)
	{
		fase_audit_fork_state(child_pid, "FAILED");
		kfree(child);
		return NULL;
	}

	*child_pid_out = child_pid;
	fase_audit_fork_state(child_pid, "CREATED");
	return child;
}

static int fork_child_mm_create(process_t *child)
{
	if (!child)
		return -ENOMEM;

	child->page_directory = (uint64_t *)create_process_page_directory();
	if (!child->page_directory)
	{
		fase_audit_fork_state(child->task.pid, "FAILED");
		return -ENOMEM;
	}

	child->owns_page_directory = 1;
	child->task.cr3 = (uint64_t)(uintptr_t)child->page_directory;
	fase_audit_fork_state(child->task.pid, "MM_CREATED");
	return 0;
}

static int fork_attach_pending_child(process_t *child, process_t *parent)
{
	uint64_t irq_flags;

	if (!child || !parent)
		return -EINVAL;

	irq_flags = process_irq_save();
	child->next = process_list;
	process_list = child;
	process_irq_restore(irq_flags);

	child->state = PROCESS_BLOCKED;
	parent->fork_pending_child = child;
	fase_audit_fork_state(child->task.pid, "DEFERRED");
	return 0;
}

void process_fork_wake_pending(process_t *parent)
{
	process_t *child;

	if (!parent)
		return;

	child = parent->fork_pending_child;
	if (!child)
		return;

	parent->fork_pending_child = NULL;
	child->state = PROCESS_READY;
	sched_add_process(child);
	fase_audit_fork_state(child->task.pid, "SCHEDULED");
	fase_audit_note_scheduled();
}

static void fork_rollback(process_t *child, pid_t child_pid, int enqueued)
{
	if (!child)
		return;

	fase_audit_fork_state(child_pid, "FAILED");
	fase_audit_fork_state(child_pid, "ROLLBACK");
	fase_audit_note_fork_rollback();

	if (enqueued)
	{
		process_t *parent = current_process;

		if (parent && parent->fork_pending_child == child)
			parent->fork_pending_child = NULL;

		if (child->state == PROCESS_READY || child->state == PROCESS_RUNNING)
			sched_remove_process(child);
		(void)process_remove_from_list(child);
	}

	fase_audit_assert_child_not_visible(child_pid);

	process_release_fds(child, "FORK_ROLLBACK");
	process_fork_destroy_child_mm(child);
	process_fork_free_mmap_list(child);

	fase_audit_fork_state(child_pid, "DESTROYED");
	process_kernel_stack_free(child);
	kfree(child);

	fase_audit_assert_child_not_visible(child_pid);
	process_fase45_fork_audit("rollback");
}

/*
 * fork() - POSIX fork for user and kernel processes.
 *
 * Clones the parent process struct and user address space (full copy, no COW),
 * duplicates the FD table, and arranges for the child to return 0 from fork().
 */
pid_t fork(void)
{
	process_t *parent = current_process;
	process_t *child;
	pid_t child_pid;

	if (!parent)
		return -1;
#if IR0_DEBUG_PROC
	if (!fase46_frames_baseline_set && parent->task.pid == 1)
	{
		size_t total_frames = 0;
		size_t used_frames = 0;

		pmm_stats(&total_frames, &used_frames, NULL);
		fase46_frames_baseline = (uint64_t)used_frames;
		fase46_frames_baseline_set = 1;
	}
	process_fase43_proc_audit("fork-before");
	process_fase44_list_checkpoint("fork-before");
	process_fase45_fork_audit("fork-before");
	paging_fase42_checkpoint("fork-before", (int32_t)parent->task.pid);
#endif

	child = fork_process_create(parent, &child_pid);
	if (!child)
		return -ENOMEM;

	if (fork_child_mm_create(child) != 0)
	{
		fork_rollback(child, child_pid, 0);
		return -ENOMEM;
	}

	if (copy_process_memory(parent, child) != 0)
	{
		fork_rollback(child, child_pid, 0);
		return -ENOMEM;
	}
	fase_audit_fork_state(child_pid, "MEMORY_CLONED");

	child->mmap_list = process_clone_mmap_list(parent->mmap_list);
	if (parent->mmap_list && !child->mmap_list)
	{
		fork_rollback(child, child_pid, 0);
		return -ENOMEM;
	}

	if (process_duplicate_fd_table(parent, child) != 0)
	{
		fork_rollback(child, child_pid, 0);
		return -ENOMEM;
	}

	child->task.rax = 0;
	child->task.cr3 = (uint64_t)(uintptr_t)child->page_directory;
	child->task.pid = child_pid;

#if defined(__x86_64__) || defined(__amd64__)
	if (parent->mode == USER_MODE)
	{
		/* Parent must not resume userspace with rax=0 after child ran first. */
		parent->task.rax = (uint64_t)child_pid;
		fork_fixup_user_syscall_return(parent, child);
		arch_set_fs_base(parent->fs_base);
#if IR0_DEBUG_PROC
		process_fase46_proc_log(parent, (int64_t)child_pid, "AFTER_FORK");
		process_fase46_proc_log(child, 0, "USER_ENTER");
#endif
	}
#endif

	if (fork_attach_pending_child(child, parent) != 0)
	{
		fork_rollback(child, child_pid, 0);
		return -ENOMEM;
	}

	fase_audit_note_proc_created();
#if IR0_DEBUG_PROC
	process_fase43_proc_audit("fork-after");
	fase_audit_trace_pid(child_pid, "CREATED");
	fase_audit_ref_emit(child, "fork");
	process_fase44_list_checkpoint("fork-after");
	process_fase45_fork_audit("fork-after");
	paging_fase42_checkpoint("fork-after", (int32_t)child_pid);
#endif

	return child_pid;
}


