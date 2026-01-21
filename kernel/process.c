/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Process Management
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Process lifecycle management, fork, exit, wait
 */

#include "process.h"
#include "rr_sched.h"
#include <ir0/kmem.h>
#include <mm/paging.h>
#include <drivers/serial/serial.h>
#include <ir0/permissions.h>
#include <ir0/signals.h>
#include <ir0/oops.h>
#include <string.h>
#include <stddef.h>
#include <ir0/errno.h>


static pid_t next_pid = 2;


process_t *current_process = NULL;
process_t *process_list = NULL;


void process_init(void)
{
	current_process = NULL;
	process_list = NULL;
	next_pid = 2;
	
	/* Initialize simple user system */
	init_simple_users();
}


pid_t process_get_next_pid(void)
{
	return next_pid++;
}

process_t *process_get_current(void)
{
	return current_process;
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
pid_t spawn(void (*entry)(void), const char *name, process_mode_t mode)
{
	process_t *proc;
	
	if (!entry || !name)
		return -1;
	
	proc = kmalloc(sizeof(process_t));
	if (!proc) {
		serial_print("[ERROR] Failed to allocate process structure\n");
		return -ENOMEM;
	}

	memset(proc, 0, sizeof(process_t));

	/* Basic process setup */
	proc->task.pid = next_pid++;
	proc->ppid = current_process ? current_process->task.pid : 1;
	proc->state = PROCESS_READY;
	
	/* Explicit mode specification - no magic address detection */
	proc->mode = mode;
	
	/* Create new page directory */
	proc->page_directory = (uint64_t *)create_process_page_directory();
	if (!proc->page_directory)
	{
		serial_print("[ERROR] Failed to create page directory for process\n");
		kfree(proc);
		return -ENOMEM;
	}
	proc->task.cr3 = (uint64_t)proc->page_directory;

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
	
	/* Set command name */
	strncpy(proc->comm, name, sizeof(proc->comm) - 1);
	proc->comm[sizeof(proc->comm) - 1] = '\0';

	/* Create user stack in userspace (only for USER_MODE processes) */
	if (proc->mode == USER_MODE)
	{
		/* User stack in userspace: 8KB at 0x7FFFF000 (high user address) */
		proc->stack_size = 0x2000;  /* 8KB */
		proc->stack_start = 0x7FFFF000UL;  /* High user address, aligned to page */
		
		/* Temporarily switch to process page directory to map stack */
		uint64_t old_cr3 = get_current_page_directory();
		load_page_directory((uint64_t)proc->page_directory);
		
		/* Map user stack with RW permissions */
		if (map_user_region_in_directory(proc->page_directory, proc->stack_start, proc->stack_size, PAGE_RW) != 0)
		{
			load_page_directory(old_cr3);  /* Restore original CR3 */
			kfree(proc);
			return -1;
		}
		
		/* Zero out the stack */
		memset((void *)proc->stack_start, 0, proc->stack_size);
		
		/* Restore original page directory */
		load_page_directory(old_cr3);
		
		/* Setup stack pointer at top of stack (stack grows down) */
		proc->task.rsp = proc->stack_start + proc->stack_size - 16;
		proc->task.rbp = proc->task.rsp;
	}
	else
	{
		/* Kernel mode: allocate from kernel heap (existing behavior) */
		proc->stack_size = 0x2000;
		proc->stack_start = (uint64_t)kmalloc(proc->stack_size);
		if (!proc->stack_start)
		{
			kfree(proc);
			return -1;
		}
		memset((void *)proc->stack_start, 0, proc->stack_size);
		proc->task.rsp = proc->stack_start + proc->stack_size - 16;
		proc->task.rbp = proc->task.rsp;
	}

	/* Setup task registers for clean start */
	proc->task.rip = (uint64_t)entry;
	proc->task.rflags = 0x202;
	proc->task.cs = 0x1B;
	proc->task.ss = 0x23;
	proc->task.ds = 0x23;
	proc->task.es = 0x23;
	proc->task.fs = 0x23;
	proc->task.gs = 0x23;

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
	}

	/* Add to process list */
	proc->next = process_list;
	process_list = proc;

	/* Add to scheduler */
	rr_add_process(proc);

	return proc->task.pid;
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

/* Dummy entry that immediately exits - child process behavior for fork() */
static void fork_child_entry(void)
{
	process_exit(0);
}

/* 
 * process_fork() - Fork syscall compatibility (uses spawn internally)
 * 
 * NOTE: IR0 only uses spawn() for process creation.
 * This function exists only for POSIX fork() syscall compatibility.
 * It uses spawn() internally, maintaining simplicity.
 */
pid_t process_fork(void)
{
	/* IR0 philosophy: only spawn() creates processes
	 * This function exists solely for sys_fork() compatibility
	 * It uses spawn() internally to maintain simplicity
	 */
	if (!current_process)
		return -1;
	
	/* Use spawn internally - only spawn creates processes in IR0
	 * Fork inherits parent's mode (if parent is kernel mode, child is kernel mode) */
	process_mode_t child_mode = current_process ? current_process->mode : KERNEL_MODE;
	pid_t child_pid = spawn(fork_child_entry, "fork_child", child_mode);
	
	/* Set return value for parent (child gets 0 from fork_child_entry) */
	if (child_pid > 0) {
		current_process->task.rax = child_pid;
	}
	
	return child_pid;
}

/* Find process by PID */
process_t *process_find_by_pid(pid_t pid)
{
	process_t *proc = process_list;
	
	while (proc)
	{
		if (proc->task.pid == pid)
		{
			return proc;
		}
		proc = proc->next;
	}
	
	return NULL; /* Not found */
}

/**
 * process_reparent_children - Reparent all children to init (PID 1)
 * @dying_parent: Process that is about to exit
 *
 * When a parent process dies, all its children become orphans.
 * This function reparents them to init (PID 1) so they don't become zombies.
 */
static void process_reparent_children(process_t *dying_parent)
{
	process_t *child;
	process_t *init;
	
	if (!dying_parent)
		return;
	
	/* Find init process (PID 1) */
	init = process_find_by_pid(1);
	if (!init)
	{
		/* No init process - this is a critical system error */
		serial_print("[CRITICAL] Init process (PID 1) not found during reparenting\n");
		serial_print("[CRITICAL] System integrity compromised - orphaned processes detected\n");
		/* Continue execution but log the critical error */
		return;
	}
	
	/* Find all children of dying parent */
	child = process_list;
	while (child)
	{
		if (child->ppid == dying_parent->task.pid)
		{
			/* Reparent to init */
			child->ppid = 1;
#if DEBUG_PROCESS
			serial_print("[PROCESS] Reparented child PID ");
			serial_print_hex32((uint32_t)child->task.pid);
			serial_print(" to init (PID 1)\n");
#endif
		}
		child = child->next;
	}
}

/**
 * process_reap_zombies - Automatically reap zombie children of a process
 * @parent: Parent process
 *
 * When a process exits, clean up any zombie children that were waiting for it.
 * Also used by init to periodically clean up zombies.
 */
void process_reap_zombies(process_t *parent)
{
	process_t *child;
	process_t *prev;
	process_t *next;
	
	if (!parent)
		return;
	
	child = process_list;
	prev = NULL;
	
	while (child)
	{
		next = child->next;
		
		/* Check if this is a zombie child of the parent */
		if (child->ppid == parent->task.pid && child->state == PROCESS_ZOMBIE)
		{
#if DEBUG_PROCESS
			serial_print("[PROCESS] Auto-reaping zombie child PID ");
			serial_print_hex32((uint32_t)child->task.pid);
			serial_print("\n");
#endif
			
			/* Remove from process list */
			if (prev)
			{
				prev->next = child->next;
			}
			else
			{
				process_list = child->next;
			}
			
			/* Free zombie process */
			kfree(child);
		}
		else
		{
			prev = child;
		}
		
		child = next;
	}
}

void process_exit(int code)
{
	process_t *dying = current_process;
	process_t *parent;
	
	if (!dying)
		return;

	/* Before becoming a zombie:
	 * 1. Reparent all children to init (PID 1) to avoid orphaned processes
	 * 2. Clean up any zombie children we were waiting for
	 */
	process_reap_zombies(dying);
	process_reparent_children(dying);

	/* Mark as zombie */
	dying->state = PROCESS_ZOMBIE;
	dying->exit_code = code;

	/* Send SIGCHLD to parent process if it exists */
	if (dying->ppid > 0)
	{
		parent = process_find_by_pid(dying->ppid);
		if (parent && parent->state != PROCESS_ZOMBIE)
		{
			send_signal(parent->task.pid, SIGCHLD);
		}
		else if (!parent || parent->state == PROCESS_ZOMBIE)
		{
			/* Parent is dead or zombie - reparent to init and send SIGCHLD to init */
			dying->ppid = 1;
			parent = process_find_by_pid(1);
			if (parent)
			{
				send_signal(parent->task.pid, SIGCHLD);
			}
		}
	}

	/* Remove process from scheduler - it should no longer be scheduled.
	 * The process structure remains in memory as a zombie until reaped
	 * by the parent (via wait()), but it will not consume CPU time.
	 */
	rr_remove_process(dying);
	
	/* Clear current_process if this is the current process.
	 * The scheduler will switch to another process.
	 */
	if (current_process == dying)
		current_process = NULL;
	
	/* Switch to another process - this will never return to this code.
	 * The zombie process remains in memory with its exit code for the
	 * parent to retrieve via wait().
	 */
	rr_schedule_next();
	
	/* Should never reach here - if we do, something is wrong */
	panic("process_exit: returned from scheduler");
}


int process_wait(pid_t pid, int *status)
{
	process_t *p;
	process_t *prev;
	pid_t ret;

	if (!current_process) {
		serial_print("[ERROR] process_wait called without current process context\n");
		return -ESRCH;
	}

	for(;;)
	{
		p = process_list;
		while (p)
		{
			/* Validate process pointer to detect corruption */
			if (!p) {
				serial_print("[ERROR] Process list corruption detected in waitpid\n");
				return -EFAULT;
			}
			
			/* Check if this is a child process we're waiting for */
			if (p->task.pid == pid && p->ppid == current_process->task.pid)
			{
				if (p->state == PROCESS_ZOMBIE)
				{
					if (status)
						*status = p->exit_code;

					/* Remove from process list */
					if (process_list == p)
					{
						process_list = p->next;
					}
					else
					{
						prev = process_list;
						while (prev->next != p)
							prev = prev->next;
						prev->next = p->next;
					}

					ret = p->task.pid;
					kfree(p);
					return ret;
				}
				break;
			}
			p = p->next;
		}

		/* Yield CPU while waiting */
		rr_schedule_next();
	}
}



uint64_t create_process_page_directory(void)
{
	uint64_t *pml4;
	uint64_t kernel_cr3;
	uint64_t *kernel_pml4;
	uint64_t pml4_addr;
	int i;

	/* Allocate page-aligned memory for PML4 */
	pml4 = kmalloc(4096 + 4096);
	if (!pml4)
		return 0;

	/* Align to 4096 bytes */
	pml4_addr = ((uint64_t)pml4 + 4095) & ~4095ULL;
	pml4 = (uint64_t *)pml4_addr;

	memset(pml4, 0, 4096);
	kernel_cr3 = get_current_page_directory();
	kernel_pml4 = (uint64_t *)kernel_cr3;

	/* Copy ONLY kernel space mappings (not user space)
	 * In x86-64 canonical addressing:
	 * - User space: virtual addresses 0x0000000000000000 - 0x00007FFFFFFFFFFF (PML4 indices 0-255)
	 * - Kernel space: virtual addresses 0xFFFF800000000000 - 0xFFFFFFFFFFFFFFFF (PML4 indices 256-511)
	 * 
	 * We only copy kernel space (indices 256-511) to prevent user processes from
	 * accessing kernel memory. User space entries start empty.
	 */
	for (i = 256; i < 512; i++)
	{
		if (kernel_pml4[i] & PAGE_PRESENT)
			pml4[i] = kernel_pml4[i];
	}

	/* Also copy identity mappings for low memory if needed (kernel boot code) */
	/* Copy first entry if it maps kernel initialization code (0-2MB typically) */
	if (kernel_pml4[0] & PAGE_PRESENT)
	{
		/* Only copy if it's a kernel mapping (no PAGE_USER flag) */
		if (!(kernel_pml4[0] & PAGE_USER))
			pml4[0] = kernel_pml4[0];
	}

	return (uint64_t)pml4;
}

void process_init_fd_table(process_t *process)
{
	int i;

	if (!process)
		return;

	/* Initialize all FDs as unused */
	for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
	{
		process->fd_table[i].in_use = false;
		process->fd_table[i].path[0] = '\0';
		process->fd_table[i].flags = 0;
		process->fd_table[i].offset = 0;
		process->fd_table[i].vfs_file = NULL;
	}

	/* Setup standard streams */
	process->fd_table[0].in_use = true;
	strncpy(process->fd_table[0].path, "/dev/stdin",
		sizeof(process->fd_table[0].path) - 1);

	process->fd_table[1].in_use = true;
	strncpy(process->fd_table[1].path, "/dev/stdout",
		sizeof(process->fd_table[1].path) - 1);

	process->fd_table[2].in_use = true;
	strncpy(process->fd_table[2].path, "/dev/stderr",
		sizeof(process->fd_table[2].path) - 1);
}
