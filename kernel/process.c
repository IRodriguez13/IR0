/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Process Management
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Process lifecycle management, fork, exit, wait
 */

#include "process.h"
#include "rr_sched.h"
#include <ir0/memory/kmem.h>
#include <ir0/memory/paging.h>
#include <drivers/serial/serial.h>
#include <ir0/permissions.h>
#include <string.h>


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



process_t *process_create(void (*entry)(void))
{
	process_t *proc;
	
	proc = kmalloc(sizeof(process_t));
	if (!proc)
		return NULL;

	memset(proc, 0, sizeof(process_t));

	/* Basic process setup */
	proc->task.pid = next_pid++;
	proc->ppid = 0;
	proc->state = PROCESS_READY;
	proc->page_directory = (uint64_t *)get_current_page_directory();

	/* User and permissions - default to root */
	proc->uid = 0;
	proc->gid = 0;
	proc->euid = 0;
	proc->egid = 0;
	proc->umask = 0022;

	/* Initialize current working directory */
	strcpy(proc->cwd, "/");
	
	/* Initialize command name */
	strncpy(proc->comm, "process", sizeof(proc->comm) - 1);
	proc->comm[sizeof(proc->comm) - 1] = '\0';

	/* Create stack */
	proc->stack_size = 0x2000;
	proc->stack_start = (uint64_t)kmalloc(proc->stack_size);
	if (!proc->stack_start)
	{
		kfree(proc);
		return NULL;
	}

	memset((void *)proc->stack_start, 0, proc->stack_size);

	/* Setup task registers */
	proc->task.rip = (uint64_t)entry;
	proc->task.rsp = proc->stack_start + proc->stack_size - 16;
	proc->task.rbp = proc->task.rsp;
	proc->task.rflags = 0x202;
	proc->task.cs = 0x1B;
	proc->task.ss = 0x23;
	proc->task.ds = 0x23;
	proc->task.es = 0x23;
	proc->task.fs = 0x23;
	proc->task.gs = 0x23;
	proc->task.cr3 = get_current_page_directory();

	/* Insert into global process list */
	proc->next = process_list;
	process_list = proc;

	/* Add to scheduler */
	rr_add_process(proc);

	return proc;
}

/* Simple spawn process - deterministic alternative to fork */
pid_t process_spawn(void (*entry)(void), const char *name)
{
	process_t *proc;
	
	if (!entry || !name)
		return -1;
	
	proc = kmalloc(sizeof(process_t));
	if (!proc)
		return -1;

	memset(proc, 0, sizeof(process_t));

	/* Basic process setup */
	proc->task.pid = next_pid++;
	proc->ppid = current_process ? current_process->task.pid : 1;
	proc->state = PROCESS_READY;
	proc->mode = KERNEL_MODE; /* Default to kernel mode for now */
	
	/* Create new page directory */
	proc->page_directory = (uint64_t *)create_process_page_directory();
	if (!proc->page_directory)
	{
		kfree(proc);
		return -1;
	}
	proc->task.cr3 = (uint64_t)proc->page_directory;

	/* Inherit permissions from current process or default to root */
	if (current_process) {
		proc->uid = current_process->uid;
		proc->gid = current_process->gid;
		proc->euid = current_process->euid;
		proc->egid = current_process->egid;
		proc->umask = current_process->umask;
		strcpy(proc->cwd, current_process->cwd);
	} else {
		proc->uid = ROOT_UID;
		proc->gid = ROOT_GID;
		proc->euid = ROOT_UID;
		proc->egid = ROOT_GID;
		proc->umask = DEFAULT_UMASK;
		strcpy(proc->cwd, "/");
	}
	
	/* Set command name */
	strncpy(proc->comm, name, sizeof(proc->comm) - 1);
	proc->comm[sizeof(proc->comm) - 1] = '\0';

	/* Create stack */
	proc->stack_size = 0x2000;
	proc->stack_start = (uint64_t)kmalloc(proc->stack_size);
	if (!proc->stack_start)
	{
		kfree(proc);
		return -1;
	}

	memset((void *)proc->stack_start, 0, proc->stack_size);

	/* Setup task registers for clean start */
	proc->task.rip = (uint64_t)entry;
	proc->task.rsp = proc->stack_start + proc->stack_size - 16;
	proc->task.rbp = proc->task.rsp;
	proc->task.rflags = 0x202;
	proc->task.cs = 0x1B;
	proc->task.ss = 0x23;
	proc->task.ds = 0x23;
	proc->task.es = 0x23;
	proc->task.fs = 0x23;
	proc->task.gs = 0x23;

	/* Initialize file descriptor table */
	process_init_fd_table(proc);

	/* Add to scheduler */
	rr_add_process(proc);

	return proc->task.pid;
}

/* Dummy entry point for spawned processes when no specific function is provided */
static void spawn_dummy_entry(void)
{
	/* Process just exits immediately - this is for sys_fork compatibility */
	process_exit(0);
}


pid_t process_fork(void)
{
	/* Use spawn internally - much simpler and deterministic */
	if (!current_process)
		return -1;
	
	/* For sys_fork compatibility, we spawn a process that immediately exits
	 * This maintains the syscall interface while using spawn internally */
	pid_t child_pid = process_spawn(spawn_dummy_entry, "fork_child");
	
	/* Set return value for parent (child gets 0 from spawn_dummy_entry) */
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

void process_exit(int code)
{
	if (!current_process)
		return;

	current_process->state = PROCESS_ZOMBIE;
	current_process->exit_code = code;

	/* Halt until reaped by parent */
	for (;;)
		__asm__ volatile("hlt");
}


int process_wait(pid_t pid, int *status)
{
	process_t *p;
	process_t *prev;
	pid_t ret;

	for(;;)
	{
		p = process_list;
		while (p)
		{
			/* Both pids are now of type pid_t (int32_t) */
			if ((uint32_t)p->task.pid == (uint32_t)pid && (uint32_t)p->ppid == (uint32_t)current_process->task.pid) // Conversión explícita
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

	/* Copy entire kernel mapping (includes low 32MB and high kernel) */
	for (i = 0; i < 512; i++)
		pml4[i] = kernel_pml4[i];

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
