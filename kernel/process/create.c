/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: create.c
 * Description: spawn/spawn_user/spawn_kernel and kernel stack allocation.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"

/* 
 * spawn() - Create a new process (IR0's ONLY process creation method)
 * 
 * IR0 PHILOSOPHY: Total simplicity with explicit mode specification
 * Only spawn() creates processes. No fork(), no clone(), no other methods.
 * Mode must be explicitly specified - no magic address detection.
 * 
 * This avoids fragile heuristics based on memory layout that could:
 * - Break if layout changes
 * - Allow user code to run in kernel mode
 * - Create hard-to-track bugs
 * 
 * process_fork() exists only for POSIX syscall compatibility and uses spawn() internally.
 */
int process_kernel_stack_alloc(process_t *p)
{
	void *base;

	if (!p)
		return -EINVAL;
	if (p->kstack_base)
		return 0;

	base = kmalloc_aligned_try(IR0_PROC_KSTACK_SIZE, 16);
	if (!base)
		return -ENOMEM;

	memset(base, 0, IR0_PROC_KSTACK_SIZE);
	p->kstack_base = base;
	p->kstack_top = (uint64_t)(uintptr_t)base + IR0_PROC_KSTACK_SIZE;
	p->saved_user_rsp = 0;
	return 0;
}

void process_kernel_stack_free(process_t *p)
{
	if (!p || !p->kstack_base)
		return;

	kfree_aligned(p->kstack_base);
	p->kstack_base = NULL;
	p->kstack_top = 0;
	p->saved_user_rsp = 0;
}

pid_t spawn(void (*entry)(void), const char *name, process_mode_t mode)
{
	process_t *proc;
	
	if (!entry || !name)
		return -1;

	serial_print("SERIAL: spawn: begin ");
	serial_print(name);
	serial_print("\n");
	
	proc = kmalloc_try(sizeof(process_t));
	if (!proc) {
		serial_print("[ERROR] Failed to allocate process structure\n");
		return -ENOMEM;
	}

	memset(proc, 0, sizeof(process_t));

	/* Default unlimited resource limits (Linux RLIM_INFINITY = ~0ULL). */
	for (int ri = 0; ri < IR0_RLIM_NLIMITS; ri++)
	{
		proc->rlimits[ri].rlim_cur = (uint64_t)-1;
		proc->rlimits[ri].rlim_max = (uint64_t)-1;
	}
	/* Soft NOFILE matches MAX_FDS_PER_PROCESS. */
	proc->rlimits[7].rlim_cur = MAX_FDS_PER_PROCESS; /* RLIMIT_NOFILE */
	proc->rlimits[7].rlim_max = MAX_FDS_PER_PROCESS;
	/* RLIMIT_STACK soft 8 MiB (Linux-like default). */
	proc->rlimits[3].rlim_cur = 8ULL * 1024ULL * 1024ULL;
	proc->rlimits[3].rlim_max = (uint64_t)-1;
	/* RLIMIT_CORE soft 0 (no dump by default). */
	proc->rlimits[4].rlim_cur = 0;
	proc->rlimits[4].rlim_max = (uint64_t)-1;
	/* RLIMIT_AS remains unlimited soft/hard. */

	/* Basic process setup */
	proc->task.pid = process_get_next_pid();
	proc->tgid = proc->task.pid;
	proc->sid = proc->task.pid;
	proc->pgid = proc->task.pid;
	proc->ppid = current_process ? current_process->task.pid : 0;
	proc->state = PROCESS_READY;
	
	/* Explicit mode specification - no magic address detection */
	proc->mode = mode;
	proc->owns_page_directory = 1;

	/*
	 * Kernel idle reuses active kernel CR3 (boot/kmain tables).  Avoids
	 * remapping ~48MB of supervisor pages on every idle spawn (slow + PMM).
	 */
	if (mode == KERNEL_MODE && name && strcmp(name, "idle") == 0)
	{
		uint64_t kcr3 = get_current_page_directory();

		proc->page_directory = (uint64_t *)kcr3;
		proc->task.cr3 = kcr3;
		proc->owns_page_directory = 0;
		serial_print("SERIAL: spawn: kernel CR3 shared (idle)\n");
	}
	else
	{
		proc->page_directory = (uint64_t *)create_process_page_directory();
		if (!proc->page_directory)
		{
			serial_print("[ERROR] Failed to create page directory for process\n");
			kfree(proc);
			return -ENOMEM;
		}
		serial_print("SERIAL: spawn: page directory OK\n");
		proc->task.cr3 = (uint64_t)proc->page_directory;
	}

	/* Inherit permissions from current process or default to root */
	if (current_process) {
		proc->uid = current_process->uid;
		proc->gid = current_process->gid;
		proc->euid = current_process->euid;
		proc->egid = current_process->egid;
		proc->umask = current_process->umask;
		strncpy(proc->cwd, current_process->cwd, sizeof(proc->cwd) - 1);
		proc->cwd[sizeof(proc->cwd) - 1] = '\0';
	} else {
		proc->uid = ROOT_UID;
		proc->gid = ROOT_GID;
		proc->euid = ROOT_UID;
		proc->egid = ROOT_GID;
		proc->umask = DEFAULT_UMASK;
		strncpy(proc->cwd, "/", sizeof(proc->cwd) - 1);
		proc->cwd[sizeof(proc->cwd) - 1] = '\0';
	}
	process_cred_init_groups(proc);
	if (proc->cwd[0] != '/')
	{
		strncpy(proc->cwd, "/", sizeof(proc->cwd) - 1);
		proc->cwd[sizeof(proc->cwd) - 1] = '\0';
	}
	
	/* Set command name */
	strncpy(proc->comm, name, sizeof(proc->comm) - 1);
	proc->comm[sizeof(proc->comm) - 1] = '\0';

	/* Create user stack in userspace (only for USER_MODE processes) */
	if (proc->mode == USER_MODE)
	{
		/* User stack: [USER_STACK_TOP - USER_STACK_SIZE, USER_STACK_TOP) */
		proc->stack_size = USER_STACK_SIZE;
		proc->stack_start = USER_STACK_TOP - USER_STACK_SIZE;

		/*
		 * Map under kernel CR3: map_user_region_in_directory() allocates page
		 * tables from the kernel heap and must not run with child CR3 active.
		 */
		if (map_user_region_in_directory(proc->page_directory, proc->stack_start, proc->stack_size, PAGE_RW) != 0)
		{
			serial_print("SERIAL: spawn: stack map failed\n");
			process_unmap_user_pages_all(proc->page_directory, NULL);
			goto fail_proc;
		}
		serial_print("SERIAL: spawn: stack mapped\n");

		/* Setup stack pointer just below USER_STACK_TOP (stack grows down) */
		proc->task.rsp = USER_STACK_TOP - 16;
		proc->task.rbp = proc->task.rsp;
	}
	else
	{
		/* Kernel mode: allocate from kernel heap (existing behavior) */
		proc->stack_size = 0x2000;
		proc->stack_start = (uint64_t)kmalloc_try(proc->stack_size);
		if (!proc->stack_start)
		{
			if (proc->owns_page_directory)
				process_unmap_user_pages_all(proc->page_directory, NULL);
			goto fail_proc;
		}
		memset((void *)proc->stack_start, 0, proc->stack_size);
		proc->task.rsp = proc->stack_start + proc->stack_size - 16;
		proc->task.rbp = proc->task.rsp;
	}

	/* Setup task registers for clean start */
	proc->task.rip = (uint64_t)entry;
	if (proc->mode == USER_MODE)
		proc->task.rflags = ir0_rflags_sanitize_user(RFLAGS_IF);
	else
		proc->task.rflags = RFLAGS_IF;
	if (proc->mode == KERNEL_MODE)
	{
		proc->task.cs = KERNEL_CODE_SEL;
		proc->task.ss = KERNEL_DATA_SEL;
		proc->task.ds = KERNEL_DATA_SEL;
		proc->task.es = KERNEL_DATA_SEL;
		proc->task.fs = KERNEL_DATA_SEL;
		proc->task.gs = KERNEL_DATA_SEL;
	}
	else
	{
		proc->task.cs = USER_CODE_SEL;
		proc->task.ss = USER_DATA_SEL;
		proc->task.ds = USER_DATA_SEL;
		proc->task.es = USER_DATA_SEL;
		proc->task.fs = USER_DATA_SEL;
		proc->task.gs = USER_DATA_SEL;
	}

	/* Initialize file descriptor table */
	process_init_fd_table(proc);

	/* Initialize signal handlers to default */
	proc->signal_pending = 0;
	proc->signal_mask = 0;
	proc->signal_ignored = 0;
	proc->saved_context = NULL;
	for (int i = 0; i < _NSIG; i++)
	{
		proc->signal_handlers[i] = SIG_DFL;
		proc->signal_sa_flags[i] = 0;
		proc->signal_sa_mask[i] = 0;
	}
	proc->robust_list = NULL;

	/* Private kernel stack for syscall/IRQ entry (see IR0_PROC_KSTACK_SIZE). */
	if (process_kernel_stack_alloc(proc) != 0)
	{
		serial_print("SERIAL: spawn: kernel stack alloc failed\n");
		if (proc->mode != USER_MODE && proc->stack_start)
		{
			kfree((void *)proc->stack_start);
			proc->stack_start = 0;
		}
		else if (proc->owns_page_directory)
		{
			process_unmap_user_pages_all(proc->page_directory, NULL);
		}
		goto fail_proc;
	}

	/* Add to process list */
	{
		uint64_t irq_flags = process_irq_save();
		proc->next = process_list;
		process_list = proc;
		process_irq_restore(irq_flags);
	}

	fase_audit_note_proc_created();
#if IR0_DEBUG_PROC
	process_fase43_proc_audit("spawn-after");
	fase_audit_spawn_init(proc);
	fase_audit_trace_pid(proc->task.pid, "CREATED");
	fase_audit_ref_emit(proc, "spawn");
	process_fase44_list_checkpoint("spawn-after");
#endif

	/* Add to scheduler */
	sched_add_process(proc);

	return proc->task.pid;

fail_proc:
	process_kernel_stack_free(proc);
	if (proc->stack_start && proc->mode == KERNEL_MODE)
	{
		kfree((void *)proc->stack_start);
		proc->stack_start = 0;
	}
	if (proc->page_directory && proc->owns_page_directory)
	{
		kfree_aligned(proc->page_directory);
		proc->page_directory = NULL;
	}
	kfree(proc);
	return -ENOMEM;
}

/* Convenience wrapper for user-mode processes */
pid_t spawn_user(void (*entry)(void), const char *name)
{
	return spawn(entry, name, USER_MODE);
}

/* Convenience wrapper for kernel-mode processes */
pid_t spawn_kernel(void (*entry)(void), const char *name)
{
	return spawn(entry, name, KERNEL_MODE);
}

