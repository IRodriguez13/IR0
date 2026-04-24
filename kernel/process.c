/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: process.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Process Management
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Process lifecycle management, fork, exit, wait
 */

#include "process.h"
#include <config.h>
#include "scheduler_api.h"
#include <ir0/kmem.h>
#include <ir0/pipe.h>
#include <mm/paging.h>
#include <fs/vfs.h>
#include <drivers/serial/serial.h>
#include <ir0/video_backend.h>
#include <ir0/permissions.h>
#include <ir0/signals.h>
#include <ir0/oops.h>
#include <string.h>
#include <stddef.h>
#include <ir0/errno.h>
#include <arch/common/arch_portable.h>


static pid_t next_pid = 2;

static inline uint64_t process_irq_save(void)
{
#if defined(__x86_64__) || defined(__i386__)
	uint64_t flags;
	__asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
	return flags;
#else
	arch_disable_interrupts();
	return 0;
#endif
}

static inline void process_irq_restore(uint64_t flags)
{
#if defined(__x86_64__) || defined(__i386__)
	__asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
#else
	(void)flags;
	arch_enable_interrupts();
#endif
}

/*
 * Follow one level of the page table hierarchy; returns NULL if the entry is
 * absent or a huge page (unmap path only supports 4KB walks).
 */
static uint64_t *process_pt_child(uint64_t *table, size_t index)
{
	if (!(table[index] & PAGE_PRESENT))
		return NULL;
	if (table[index] & PAGE_SIZE_2MB_FLAG)
		return NULL;
	return (uint64_t *)(table[index] & PAGE_FRAME_MASK);
}

/*
 * Drop every present PAGE_USER mapping under PML4 indices 0..255 so PMM
 * frames are returned and the address space can be discarded safely while
 * another process is active (CR3 unrelated).
 */
static void process_unmap_user_pages_all(uint64_t *pml4)
{
	size_t i4;
	size_t i3;
	size_t i2;
	size_t i1;

	if (!pml4)
		return;

	for (i4 = 0; i4 < 256; i4++)
	{
		uint64_t *pdpt = process_pt_child(pml4, i4);

		if (!pdpt)
			continue;

		for (i3 = 0; i3 < 512; i3++)
		{
			uint64_t *pd = process_pt_child(pdpt, i3);

			if (!pd)
				continue;

			for (i2 = 0; i2 < 512; i2++)
			{
				uint64_t *pt = process_pt_child(pd, i2);

				if (!pt)
					continue;

				for (i1 = 0; i1 < 512; i1++)
				{
					uint64_t ent = pt[i1];
					uintptr_t virt;

					if (!(ent & PAGE_PRESENT) || !(ent & PAGE_USER))
						continue;

					virt = ((uintptr_t)i4 << 39) | ((uintptr_t)i3 << 30) |
					       ((uintptr_t)i2 << 21) | ((uintptr_t)i1 << 12);
					unmap_page_in_directory(pml4, virt);
				}
			}
		}
	}
}


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
	uint64_t irq_flags = process_irq_save();
	pid_t pid = next_pid++;
	process_irq_restore(irq_flags);
	return pid;
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
	
	proc = kmalloc_try(sizeof(process_t));
	if (!proc) {
		serial_print("[ERROR] Failed to allocate process structure\n");
		return -ENOMEM;
	}

	memset(proc, 0, sizeof(process_t));

	/* Basic process setup */
	proc->task.pid = process_get_next_pid();
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
		/* User stack: [USER_STACK_TOP - USER_STACK_SIZE, USER_STACK_TOP) */
		proc->stack_size = USER_STACK_SIZE;
		proc->stack_start = USER_STACK_TOP - USER_STACK_SIZE;
		
		/* Temporarily switch to process page directory to map stack */
		uint64_t old_cr3 = get_current_page_directory();
		load_page_directory((uint64_t)proc->page_directory);
		
		/* Map user stack with RW permissions */
		if (map_user_region_in_directory(proc->page_directory, proc->stack_start, proc->stack_size, PAGE_RW) != 0)
		{
			load_page_directory(old_cr3);  /* Restore original CR3 */
			process_unmap_user_pages_all(proc->page_directory);
			goto fail_proc;
		}
		
		/* Zero out the stack */
		memset((void *)proc->stack_start, 0, proc->stack_size);
		
		/* Restore original page directory */
		load_page_directory(old_cr3);
		
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
			process_unmap_user_pages_all(proc->page_directory);
			goto fail_proc;
		}
		memset((void *)proc->stack_start, 0, proc->stack_size);
		proc->task.rsp = proc->stack_start + proc->stack_size - 16;
		proc->task.rbp = proc->task.rsp;
	}

	/* Setup task registers for clean start */
	proc->task.rip = (uint64_t)entry;
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
	}

	/* Add to process list */
	{
		uint64_t irq_flags = process_irq_save();
		proc->next = process_list;
		process_list = proc;
		process_irq_restore(irq_flags);
	}

	/* Add to scheduler */
	sched_add_process(proc);

	return proc->task.pid;

fail_proc:
	if (proc->stack_start && proc->mode == KERNEL_MODE)
	{
		kfree((void *)proc->stack_start);
		proc->stack_start = 0;
	}
	if (proc->page_directory)
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

/*
 * Entry for the lightweight child created by fork() below. This is not a
 * POSIX child: it does not resume at the fork() syscall site with return
 * value zero, and it does not inherit a duplicated address space.
 */
static void fork_child_entry(void)
{
	process_exit(0);
}

/*
 * fork() — kept for syscall wiring and historical callers; not a real fork.
 *
 * The debug shell uses SYS_FORK when building pipelines (pipe + two children).
 * IR0 does not copy the parent address space or duplicate register state at
 * the syscall boundary. The child is a new task that runs fork_child_entry and
 * exits immediately. That is intentional given the missing copy-on-write /
 * address-space machinery; use kexecve for normal program loading.
 *
 * The parent receives the new PID as the fork() return value. Expecting full
 * POSIX fork semantics in user code will not work until address-space copy is
 * implemented.
 */
pid_t fork(void)
{
	if (!current_process)
		return -1;

	process_mode_t child_mode = current_process->mode;
	pid_t child_pid = spawn(fork_child_entry, "fork_child", child_mode);

	if (child_pid > 0)
		current_process->task.rax = child_pid;

	return child_pid;
}

/* Find process by PID */
process_t *process_find_by_pid(pid_t pid)
{
	process_t *proc;
	uint64_t irq_flags = process_irq_save();

	proc = process_list;
	
	while (proc)
	{
		if (proc->task.pid == pid)
		{
			process_irq_restore(irq_flags);
			return proc;
		}
		proc = proc->next;
	}
	
	process_irq_restore(irq_flags);
	return NULL; /* Not found */
}

int process_remove_from_list(process_t *target)
{
	process_t *scan;
	process_t *prev;
	uint64_t irq_flags;

	if (!target)
		return -EINVAL;

	irq_flags = process_irq_save();
	prev = NULL;
	scan = process_list;

	while (scan)
	{
		if (scan == target)
		{
			if (prev)
				prev->next = scan->next;
			else
				process_list = scan->next;
			scan->next = NULL;
			process_irq_restore(irq_flags);
			return 0;
		}
		prev = scan;
		scan = scan->next;
	}

	process_irq_restore(irq_flags);
	return -ENOENT;
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
	process_t *next;
	
	if (!parent)
		return;
	
	child = process_list;
	
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
			
			if (process_remove_from_list(child) == 0)
			{
				/* Free zombie process */
				process_destroy(child);
				kfree(child);
			}
		}
		else
		{
			/* Keep scanning siblings. */
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
	sched_remove_process(dying);
	
	/* Switch to another process - this will never return to this code.
	 * The zombie process remains in memory with its exit code for the
	 * parent to retrieve via wait().
	 */
	sched_schedule_next();
	
	/* Should never reach here - if we do, something is wrong */
	panic("process_exit: returned from scheduler");
}


/*
 * process_destroy - Release per-process resources before freeing a zombie struct.
 * Closes VFS and pipe handles, clears the FD table, and tears down user mappings
 * in this process's page directory (not necessarily the active CR3).
 */
void process_destroy(process_t *p)
{
	int i;
	struct mmap_region *r;
	struct mmap_region *next;

	if (!p)
		return;

	serial_print("[PROCESS] destroy PID ");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" (fd cleanup)\n");

	for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
	{
		fd_entry_t *e = &p->fd_table[i];

		if (!e->in_use)
			continue;

		if (i >= 1000 && i <= 3999)
		{
			/* /proc, /dev, /sys pseudo-fds: no kernel object to release */
			goto clear_fd;
		}

		if (i <= 2)
			goto clear_fd;

		if (e->is_pipe && e->vfs_file)
		{
			pipe_close_end((pipe_t *)e->vfs_file, e->pipe_end);
			e->vfs_file = NULL;
		}
		else if (e->vfs_file)
		{
			vfs_close((struct vfs_file *)e->vfs_file);
			e->vfs_file = NULL;
		}

clear_fd:
		e->in_use = false;
		e->is_pipe = false;
		e->pipe_end = -1;
		e->path[0] = '\0';
		e->flags = 0;
		e->fd_flags = 0;
		e->offset = 0;
	}

	/* Unmap all user pages in this process's PML4 (reaper may run under another CR3) */
	if (p->page_directory)
		process_unmap_user_pages_all(p->page_directory);

	/* Drop mmap bookkeeping nodes associated with this process. */
	r = p->mmap_list;
	while (r)
	{
		next = r->next;
		kfree(r);
		r = next;
	}
	p->mmap_list = NULL;

	if (p->saved_context)
	{
		kfree(p->saved_context);
		p->saved_context = NULL;
	}

	if (p->mode == KERNEL_MODE && p->stack_start &&
	    p->stack_start != INIT_DEBUG_STACK_BASE)
	{
		kfree((void *)p->stack_start);
		p->stack_start = 0;
		p->stack_size = 0;
	}

	if (p->page_directory)
	{
		kfree_aligned(p->page_directory);
		p->page_directory = NULL;
	}
}

int process_wait(pid_t pid, int *status, int options)
{
	process_t *p;
	pid_t ret;
	int found_child;
	process_t *zombie;
	uint64_t irq_flags;
	/*
	 * waitpid-style: pid > 0 waits for that child; pid == -1 or pid == 0
	 * waits for any child of the caller (process groups not implemented).
	 */
	const int any_child = (pid == (pid_t)-1 || pid == 0);

	if (!current_process) {
		serial_print("[ERROR] process_wait called without current process context\n");
		return -ESRCH;
	}

	for (;;) {
		found_child = 0;
		zombie = NULL;
		irq_flags = process_irq_save();

		for (p = process_list; p; p = p->next) {
			if (p->ppid != current_process->task.pid)
				continue;
			if (!any_child && p->task.pid != pid)
				continue;

			found_child = 1;
			if (p->state == PROCESS_ZOMBIE) {
				zombie = p;
				break;
			}
		}

		if (zombie) {
			process_t *scan = process_list;
			process_t *prev = NULL;
			while (scan) {
				if (scan == zombie) {
					if (prev)
						prev->next = scan->next;
					else
						process_list = scan->next;
					scan->next = NULL;
					break;
				}
				prev = scan;
				scan = scan->next;
			}
		}
		process_irq_restore(irq_flags);

		if (zombie) {
			if (status)
				*status = (zombie->exit_code & 0xFF) << 8;

			ret = zombie->task.pid;
			process_destroy(zombie);
			kfree(zombie);
			return ret;
		}

		if (!found_child)
			return -ECHILD;

		/* Children exist but none are zombies yet */
		if (options & WNOHANG)
			return 0;

		sched_schedule_next();
	}
}



uint64_t create_process_page_directory(void)
{
	uint64_t *pml4;
	uint64_t kernel_cr3;
	uint64_t *kernel_pml4;
	int i;

	/* Allocate page-aligned memory for PML4 */
	pml4 = kmalloc_aligned(4096, 4096);
	if (!pml4)
		return 0;

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

	/*
	 * Explicitly map framebuffer into process so console output is visible.
	 * Framebuffer is often above 32MB (e.g. 0xFD000000) and may not be
	 * in the copied low-memory mapping.
	 */
#if CONFIG_ENABLE_VBE
	if (video_backend_is_available() && video_backend_get_fb_phys() != 0)
	{
		uint32_t fb_phys = video_backend_get_fb_phys();
		uint32_t fb_size = video_backend_get_fb_size();
		for (uint32_t off = 0; off < fb_size; off += 4096)
		{
			uint64_t p = fb_phys + off;
			if (map_page_in_directory(pml4, p, p, PAGE_PRESENT | PAGE_RW) != 0)
				break;
		}
	}
#endif

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
		process->fd_table[i].fd_flags = 0;
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
