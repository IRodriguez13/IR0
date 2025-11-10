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
#include <string.h>


static pid_t next_pid = 2;


process_t *current_process = NULL;
process_t *process_list = NULL;


void process_init(void)
{
	current_process = NULL;
	process_list = NULL;
	next_pid = 2;
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


pid_t process_fork(void)
{
	process_t *child;
	uint64_t new_cr3;
	void *new_stack;
	uint64_t offset_rsp;
	uint64_t offset_rbp;

	if (!current_process)
		return -1;

	/* Allocate child process structure */
	child = kmalloc(sizeof(process_t));
	if (!child)
		return -1;

	/* Copy parent process */
	memcpy(child, current_process, sizeof(process_t));

	/* Assign new PID and set parent */
	child->task.pid = next_pid++;
	child->ppid = current_process->task.pid;
	child->state = PROCESS_READY;
	child->exit_code = 0;

	/* Create new page directory for child */
	new_cr3 = create_process_page_directory();
	if (!new_cr3)
	{
		goto cleanup;
		return -1;
	}
	child->task.cr3 = new_cr3;
	child->page_directory = (uint64_t *)new_cr3;

	/* Clone stack */
	new_stack = kmalloc(current_process->stack_size);
	if (!new_stack)
	{
		goto cleanup;
		return -1;
	}
	memcpy(new_stack, (void *)current_process->stack_start,
	       current_process->stack_size);

	/* Adjust stack pointers (stack grows down) */
	offset_rsp = (current_process->stack_start + current_process->stack_size)
		     - current_process->task.rsp;
	offset_rbp = (current_process->stack_start + current_process->stack_size)
		     - current_process->task.rbp;

	child->stack_start = (uint64_t)new_stack;
	child->stack_size = current_process->stack_size;
	child->task.rsp = child->stack_start + child->stack_size - offset_rsp;
	child->task.rbp = child->stack_start + child->stack_size - offset_rbp;

	/* Align stack to 16 bytes */
	child->task.rsp &= ~0xF;

	/* Set return values for fork */
	child->task.rax = 0;
	current_process->task.rax = child->task.pid;

	/* Inherit current working directory from parent */
	strcpy(child->cwd, current_process->cwd);

	/* Insert into process list and scheduler */
	child->next = process_list;
	process_list = child;
	rr_add_process(child);

	cleanup:
		kfree(child);
		
	return child->task.pid;
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
			// Both pids are now of type pid_t (int32_t)
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
