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
#include <ir0/arch_debug.h>
#include <ir0/task_ops.h>

static void syscall_frame_to_arch(const syscall_user_frame_t *sf,
				  arch_task_syscall_frame_t *out)
{
	if (!sf || !out)
		return;

	*out = *sf;
}

int process_task_kernel_return_state_bad(const task_t *t)
{
	if (!t)
		return 0;
	return !task_return_state_is_user(t) &&
	       mm_user_va_ok((uintptr_t)task_get_ip(t), 1);
}

uint64_t process_list_count(void)
{
	process_t *p;
	uint64_t n = 0;
	uint64_t irq_flags = process_registry_lock();

	for (p = process_list; p; p = p->next)
		n++;
	process_registry_unlock(irq_flags);
	return n;
}

uint64_t process_list_count_user(void)
{
	process_t *p;
	uint64_t n = 0;
	uint64_t irq_flags = process_registry_lock();

	for (p = process_list; p; p = p->next)
	{
		if (p->mode == USER_MODE && p->state != PROCESS_ZOMBIE)
			n++;
	}
	process_registry_unlock(irq_flags);
	return n;
}

void process_init(void)
{
	process_set_current(NULL);
	process_registry_reset();
	debug_state_init();
}

void process_guest_install(process_t *guest)
{
	if (!guest)
		return;
	process_set_current(guest);
	process_registry_install_if_empty(guest);
}

void process_guest_rearm(process_t *guest)
{
	if (!guest)
		return;
	guest->lifecycle = PROCESS_LIFECYCLE_ALIVE;
	guest->state = PROCESS_RUNNING;
	process_set_current(guest);
}


process_t *process_get_current(void)
{
	return current_process;
}

/*
 * Copy user context from an opaque ISA exception frame into the current task.
 * Frame classification and decoding remain architecture-owned.
 */
void process_save_user_exception_frame(void *frame)
{
	process_t *p;

	if (!frame)
		return;

	p = current_process;
	if (!p || p->mode != USER_MODE)
		return;

	if (!exception_frame_is_user(frame))
		return;

#if CONFIG_DEBUG_ISRABI
	klog_debug_fmt("ISR", "[ISRABI][IRQ_SAVE] pid=%x source=opaque_arch_frame",
		       (unsigned)(current_process
			? (uint32_t)current_process->task.pid : 0));
#endif

	task_save_user_exception_frame(&p->task, frame);

#if CONFIG_DEBUG_ISRABI
	klog_debug_fmt("ISR", "[ISRABI][IRQ_SAVE] task_ip=%llx task_sp=%llx return_user=%llx task_status=%llx", (unsigned long long)(task_get_ip(&p->task)), (unsigned long long)(task_get_sp(&p->task)), (unsigned long long)task_return_state_is_user(&p->task), (unsigned long long)(task_get_flags(&p->task)));
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

process_t *process_find_by_pid(pid_t pid)
{
	process_t *proc;
	uint64_t irq_flags = process_registry_lock();

	for (proc = process_list; proc; proc = proc->next)
	{
		if (proc->task.pid == pid)
		{
			process_registry_unlock(irq_flags);
			return proc;
		}
	}

	process_registry_unlock(irq_flags);
	return NULL;
}

/*
 * process_capture_syscall_frame_at_entry - Snapshot user GPRs at syscall entry.
 * Layout decode is ISA-private (arch_syscall_frame).
 */
void process_capture_syscall_frame_at_entry(uint64_t *frame_base, uint64_t rip_hw)
{
	syscall_capture_frame_at_entry(current_process, frame_base,
						    rip_hw);
}

/*
 * Soft-mirror the saved syscall frame while the task still has a valid
 * userspace-return state.
 */
void process_sync_task_user_ip_from_syscall_frame(process_t *p)
{
	syscall_user_frame_t *sf;

	if (!p || p->mode != USER_MODE)
		return;
	if (!task_cs_is_user(&p->task))
		return;

	sf = &p->syscall_frame;
	task_sync_syscall_soft_mirror(&p->task, sf);
}

void process_apply_syscall_frame_to_task(task_t *task, const syscall_user_frame_t *sf,
                                         uint64_t rax)
{
	arch_task_syscall_frame_t arch_sf;

	if (!task || !sf)
		return;

	syscall_frame_to_arch(sf, &arch_sf);
	task_apply_syscall_frame(task, &arch_sf, rax);
}

void process_syscall_restore_exit_regs(uint64_t *exit_frame)
{
	syscall_restore_exit_regs(current_process, exit_frame);
}

void process_restore_user_task_segments(process_t *p)
{
	if (!p || p->mode != USER_MODE)
		return;

	task_apply_user_segments(&p->task);
}
