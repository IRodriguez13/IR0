/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: core.c
 * Description: Process list, PID allocation, init, and syscall-frame resume helpers.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"

static pid_t next_pid = 2;

uint64_t process_list_count(void)
{
	process_t *p;
	uint64_t n = 0;

	for (p = process_list; p; p = p->next)
		n++;
	return n;
}

uint64_t process_list_count_user(void)
{
	process_t *p;
	uint64_t n = 0;

	for (p = process_list; p; p = p->next)
	{
		if (p->mode == USER_MODE && p->state != PROCESS_ZOMBIE)
			n++;
	}
	return n;
}

void process_fase50_trace_proc(const char *stage, process_t *p)
{
	(void)stage;
	(void)p;
}

process_t *current_process = NULL;
process_t *process_list = NULL;


void process_init(void)
{
	current_process = NULL;
	process_list = NULL;
	ir0_debug_trap_init();
#if KERNEL_DEBUG_SHELL
	/* PID 1 reserved for debug-shell init (start_init_process hardcodes pid 1). */
	next_pid = 2;
#else
	/* First spawned process is /sbin/init (PID 1). */
	next_pid = 1;
#endif

	/* Initialize simple user system */
	init_simple_users();
}


pid_t process_get_next_pid(void)
{
	uint64_t irq_flags = process_irq_save();
	pid_t pid = next_pid++;
	process_irq_restore(irq_flags);
	return pid;
}

/*
 * KTM boot scenarios (and similar early probes) may advance next_pid.
 * runit-init / BusyBox init require getpid()==1. Restore the allocator so the
 * first userspace spawn is PID 1 when that slot is free.
 */
void process_prepare_pid1_for_init(void)
{
	uint64_t irq_flags;

	if (process_find_by_pid(1))
		return;

	irq_flags = process_irq_save();
	next_pid = 1;
	process_irq_restore(irq_flags);
}

process_t *process_get_current(void)
{
	return current_process;
}

/*
 * irq_save_user_frame - Copy the full user context from the IRQ stub stack into
 * the current task.
 *
 * @frame: pointer to the iretq frame on the ISR stub stack
 *         (frame[0..6] = int_no, err, RIP, CS, RFLAGS, RSP, SS). The 15 saved
 *         GPRs sit immediately BELOW it (isr_common_stub_64 push order), so they
 *         are reachable at frame[-1..-15]:
 *           [-1]=rax [-2]=rcx [-3]=rdx [-4]=rbx [-5]=rbp [-6]=rsi [-7]=rdi
 *           [-8]=r8  [-9]=r9  [-10]=r10 [-11]=r11 [-12]=r12 [-13]=r13
 *           [-14]=r14 [-15]=r15
 *
 * Saving the GPRs (not just RIP/RSP/RFLAGS) keeps task_t coherent if a user task
 * is ever resumed via switch_context_x64 .user_iretq_resume from this snapshot
 * (e.g. when the IRQ preempt path is wired): a partial save would resume the
 * task with stale GPRs and corrupt user computation.
 */
void irq_save_user_frame(uint64_t *frame)
{
	process_t *p;

	if (!frame)
		return;

	p = current_process;
	if (!p || p->mode != USER_MODE)
		return;

	if ((frame[3] & 3U) != 3U)
		return;

#if CONFIG_DEBUG_ISRABI
	serial_print("[ISRABI][IRQ_SAVE] pid=");
	serial_print_hex32(current_process ? (uint32_t)current_process->task.pid : 0);
	serial_print(" src_int=");
	serial_print_hex64(frame[0]);
	serial_print(" src_err=");
	serial_print_hex64(frame[1]);
	serial_print(" src_rip=");
	serial_print_hex64(frame[2]);
	serial_print(" src_cs=");
	serial_print_hex64(frame[3]);
	serial_print(" src_rflags=");
	serial_print_hex64(frame[4]);
	serial_print(" src_rsp=");
	serial_print_hex64(frame[5]);
	serial_print(" src_ss=");
	serial_print_hex64(frame[6]);
	serial_print("\n");
#endif

	p->task.rip = frame[2];
	p->task.rflags = ir0_rflags_sanitize_user((frame[4] | 2ULL) | RFLAGS_IF);
	p->task.rsp = frame[5];

	/* Full user GPR set from the stub stack (below the iretq frame). */
	p->task.rax = frame[-1];
	p->task.rcx = frame[-2];
	p->task.rdx = frame[-3];
	p->task.rbx = frame[-4];
	p->task.rbp = frame[-5];
	p->task.rsi = frame[-6];
	p->task.rdi = frame[-7];
	p->task.r8 = frame[-8];
	p->task.r9 = frame[-9];
	p->task.r10 = frame[-10];
	p->task.r11 = frame[-11];
	p->task.r12 = frame[-12];
	p->task.r13 = frame[-13];
	p->task.r14 = frame[-14];
	p->task.r15 = frame[-15];

	if ((frame[3] & 3U) == 3U)
	{
		p->task.cs = (uint16_t)USER_CODE_SEL;
		p->task.ss = (uint16_t)USER_DATA_SEL;
	}
	else
	{
		p->task.cs = (uint16_t)frame[3];
		p->task.ss = (uint16_t)frame[6];
	}
	/* irq_frame_saved is set only when blocking in process_wait(), not on timer IRQ. */

#if CONFIG_DEBUG_ISRABI
	serial_print("[ISRABI][IRQ_SAVE] task_rip=");
	serial_print_hex64(p->task.rip);
	serial_print(" task_rsp=");
	serial_print_hex64(p->task.rsp);
	serial_print(" task_cs=");
	serial_print_hex64((uint64_t)p->task.cs);
	serial_print(" task_ss=");
	serial_print_hex64((uint64_t)p->task.ss);
	serial_print(" task_rflags=");
	serial_print_hex64(p->task.rflags);
	serial_print("\n");
#endif
}

pid_t process_get_pid(void)
{
	return current_process ? process_pid(current_process) : 0;
}

pid_t process_get_ppid(void)
{
	return current_process ? current_process->ppid : 0;
}

process_t *get_process_list(void)
{
	return process_list;
}



int process_validate_userspace_buffer(const void *buf, size_t size)
{
	if (!current_process)
		return -ESRCH;

	if (current_process->mode == KERNEL_MODE)
	{
		uint64_t addr = (uint64_t)buf;

		if (addr >= current_process->stack_start &&
		    addr + size <= current_process->stack_start + current_process->stack_size)
			return 0;
		if (current_process->heap_start > 0 &&
		    addr >= current_process->heap_start &&
		    addr + size <= current_process->heap_end)
			return 0;
		if (is_user_address(buf, size))
			return 0;
		return 0;
	}

	if (!is_user_address(buf, size))
		return -EFAULT;

	return 0;
}

#if defined(__x86_64__) || defined(__amd64__)
extern uint64_t fase29_entry_rip;

/*
 * process_capture_syscall_frame - Snapshot user GPRs at syscall dispatch entry.
 *
 * Must run before nested C calls (fork, wait4, execve path) clobber the
 * syscall kernel stack layout (Linux pt_regs at entry).
 */

/*
 * process_capture_syscall_frame_at_entry - Linux SAVE_ALL at syscall entry.
 *
 * @frame_base: RSP at the rbx slot (see syscall_insn_entry_64.asm).
 * @rip_hw: optional hardware user RIP; asm does not pass this today.
 */
void process_capture_syscall_frame_at_entry(uint64_t *frame_base, uint64_t rip_hw)
{
	process_t *p = current_process;
	syscall_user_frame_t *sf;

	if (!frame_base || !p || p->mode != USER_MODE)
		return;

	sf = &p->syscall_frame;
	/*
	 * frame_base[7] is the user RIP (rcx) pushed at this syscall entry.
	 * fase29_entry_rip is written at the *previous* syscall's sysret and must
	 * not override the current entry snapshot (breaks fork child iretq).
	 * rip_hw is not passed from asm today; only use as last resort.
	 */
	sf->rip = frame_base[7];
	if (!sf->rip && rip_hw)
		sf->rip = rip_hw;
	sf->rflags = frame_base[6];
	sf->rsp = frame_base[8];
	sf->rbx = frame_base[0];
	sf->rbp = frame_base[1];
	sf->r12 = frame_base[2];
	sf->r13 = frame_base[3];
	sf->r14 = frame_base[4];
	sf->r15 = frame_base[5];
	/* Linux ABI args sit below the callee-saved block on the syscall stack. */
	sf->rdi = frame_base[-1];
	sf->rsi = frame_base[-2];
	sf->rdx = frame_base[-3];
	sf->r10 = frame_base[-4];
	sf->r8 = frame_base[-5];
	sf->r9 = frame_base[-6];
	/*
	 * Mark this task as having a fresh Linux pt_regs snapshot for this entry.
	 * Only the `syscall` insn path reaches here; int 0x80 tasks never set it,
	 * which keeps the cooperative syscall_frame resume restricted to musl.
	 */
	p->syscall_frame_fresh = 1;
	process_sync_task_user_ip_from_syscall_frame(p);
}

void process_sync_task_user_ip_from_syscall_frame(process_t *p)
{
	syscall_user_frame_t *sf;

	if (!p || p->mode != USER_MODE)
		return;

	sf = &p->syscall_frame;
	p->task.rip = sf->rip;
	p->task.rsp = sf->rsp;
	p->task.rflags = ir0_rflags_sanitize_user(sf->rflags | 2ULL);
	p->task.rcx = sf->rip;
	p->task.r11 = p->task.rflags;
}

void process_capture_syscall_frame(process_t *p)
{
	(void)p;
}

void process_apply_syscall_frame_to_task(task_t *task, const syscall_user_frame_t *sf,
                                         uint64_t rax)
{
	if (!task || !sf)
		return;

	task->rip = sf->rip;
	task->rsp = sf->rsp;
	task->rflags = ir0_rflags_sanitize_user(sf->rflags | 2ULL);
	task->rax = rax;
	task->rbx = sf->rbx;
	task->rbp = sf->rbp;
	task->r12 = sf->r12;
	task->r13 = sf->r13;
	task->r14 = sf->r14;
	task->r15 = sf->r15;
	task->rdi = sf->rdi;
	task->rsi = sf->rsi;
	task->rdx = sf->rdx;
	task->r10 = sf->r10;
	task->r8 = sf->r8;
	task->r9 = sf->r9;
	task->rcx = sf->rip;
	task->r11 = ir0_rflags_sanitize_user(sf->rflags | 2ULL);
	task->cs = USER_CODE_SEL;
	task->ss = USER_DATA_SEL;
	task->ds = USER_DATA_SEL;
	task->es = USER_DATA_SEL;
	task->fs = USER_DATA_SEL;
	task->gs = USER_DATA_SEL;
}

/*
 * process_syscall_restore_exit_regs - Linux RESTORE_ALL before sysret.
 *
 * Repopulate the syscall stack pt_regs mirror from current->syscall_frame so
 * nested C in fork/wait4 cannot clobber user GPRs (musl TLS in %rdx, etc.).
 *
 * @stack_r9_slot: RSP at the saved-r9 word (see syscall_insn_entry_64.asm).
 */
void process_syscall_restore_exit_regs(uint64_t *stack_r9_slot)
{
	process_t *p = current_process;
	const syscall_user_frame_t *sf;

	if (!stack_r9_slot || !p || p->mode != USER_MODE)
		return;

	sf = &p->syscall_frame;
	stack_r9_slot[0] = sf->r9;
	stack_r9_slot[1] = sf->r8;
	stack_r9_slot[2] = sf->r10;
	stack_r9_slot[3] = sf->rdx;
	stack_r9_slot[4] = sf->rsi;
	stack_r9_slot[5] = sf->rdi;
	stack_r9_slot[6] = sf->rbx;
	stack_r9_slot[7] = sf->rbp;
	stack_r9_slot[8] = sf->r12;
	stack_r9_slot[9] = sf->r13;
	stack_r9_slot[10] = sf->r14;
	stack_r9_slot[11] = sf->r15;
	stack_r9_slot[12] = sf->rflags;
	stack_r9_slot[13] = sf->rip;
	p->fork_resync_syscall_stack = 0;
}

void process_arm_blocked_syscall_resume(process_t *p, uint64_t rax)
{
	if (!p || p->mode != USER_MODE)
		return;

	process_apply_syscall_frame_to_task(&p->task, &p->syscall_frame, rax);
	p->syscall_resume_rax = rax;
	p->irq_frame_saved = 1;
}

/*
 * process_arm_coop_resched_resume - Arm a cooperative in-syscall reschedule to
 * resume via the saved syscall_frame (fresh iretq) instead of kernel_ret on the
 * shared global syscall stack. Unlike wait4, there is no zombie child to reap,
 * so coop_resched_resume tells arch_context_switch to skip the reap step.
 * Only valid for syscall-insn tasks (syscall_frame_fresh).
 */
void process_arm_coop_resched_resume(process_t *p, uint64_t rax)
{
	if (!p || p->mode != USER_MODE)
		return;

	process_apply_syscall_frame_to_task(&p->task, &p->syscall_frame, rax);
	p->syscall_resume_rax = rax;
	p->coop_resched_resume = 1;
	p->irq_frame_saved = 1;
}

/*
 * process_clear_in_thread_syscall_block - Drop irq_frame_saved after blocking
 * syscalls that resume inside the syscall handler (poll/pipe read loops), not
 * via arch_switch_to_user_task.
 */
void process_clear_in_thread_syscall_block(process_t *p)
{
	if (!p)
		return;

	p->irq_frame_saved = 0;
	p->poll_resume_via_arch = 0;
	p->coop_resched_resume = 0;
}

void process_reset_blocked_syscall_state(process_t *p)
{
	if (!p)
		return;

	p->irq_frame_saved = 0;
	p->poll_resume_via_arch = 0;
	p->coop_resched_resume = 0;
	p->syscall_resume_rax = 0;
	p->syscall_interrupted = 0;
	p->wait_status_ptr = NULL;
	p->wait_blocked = 0;
	p->wait_blocked = 0;
	p->wait_target_pid = 0;
	p->wait_options = 0;
	p->wait_resume_child_pid = 0;
	p->poll_waiter = NULL;
	p->clock_wait_armed = 0;
	p->clock_wait_deadline_ms = IR0_CLOCK_WAIT_DISARMED;
}

/*
 * process_arm_kernel_syscall_sleep - Mark task as ring-0 for switch_context resume.
 * Used when blocking inside a syscall without irq_frame_saved user return.
 */
void process_arm_kernel_syscall_sleep(process_t *p)
{
	if (!p || p->mode != USER_MODE)
		return;

	p->task.cs = KERNEL_CODE_SEL;
	p->task.ss = KERNEL_DATA_SEL;
	p->task.ds = KERNEL_DATA_SEL;
	p->task.es = KERNEL_DATA_SEL;
	p->task.fs = KERNEL_DATA_SEL;
	p->task.gs = KERNEL_DATA_SEL;
}

void process_restore_user_task_segments(process_t *p)
{
	if (!p || p->mode != USER_MODE)
		return;

	p->task.cs = USER_CODE_SEL;
	p->task.ss = USER_DATA_SEL;
	p->task.ds = USER_DATA_SEL;
	p->task.es = USER_DATA_SEL;
	p->task.fs = USER_DATA_SEL;
	p->task.gs = USER_DATA_SEL;
}


#if defined(__x86_64__) || defined(__amd64__)
void process_save_user_context_from_irq_frame(uint64_t *gpr_stack)
{
	/*
	 * gpr_stack points at the saved-RAX slot on the ISR stub stack; the
	 * iretq frame begins 15 qwords above (see isr_common_stub_64 / sched_resched.c).
	 */
	if (!gpr_stack)
		return;

	irq_save_user_frame(gpr_stack + 15);
}
#endif

#endif /* __x86_64__ */

