/* SPDX-License-Identifier: GPL-3.0-only */
/* Single owner for the architecture-neutral process registry state. */

#include "process_internal.h"
#include <ir0/percpu.h>

process_t *current_process;
process_t *process_list;
static pid_t next_pid = 2;

void process_set_current(process_t *process)
{
	current_process = process;
	cpu_local_set_current_task(process ? &process->task : NULL);
}

void process_registry_reset(void)
{
	uint64_t irq_flags = process_registry_lock();

	process_list = NULL;
	next_pid = 1;
	process_registry_unlock(irq_flags);
}

void process_registry_insert(process_t *process)
{
	uint64_t irq_flags;

	if (!process)
		return;
	irq_flags = process_registry_lock();
	process->next = process_list;
	process_list = process;
	if (process->task.pid >= next_pid)
		next_pid = (pid_t)(process->task.pid + 1);
	process_registry_unlock(irq_flags);
}

process_t *process_find_child_of(pid_t ppid)
{
	process_t *proc;
	uint64_t irq_flags = process_registry_lock();

	for (proc = process_list; proc; proc = proc->next)
	{
		if (proc->ppid == ppid &&
		    proc->lifecycle == PROCESS_LIFECYCLE_ALIVE)
		{
			process_registry_unlock(irq_flags);
			return proc;
		}
	}
	process_registry_unlock(irq_flags);
	return NULL;
}

void process_registry_install_if_empty(process_t *process)
{
	uint64_t irq_flags;

	if (!process)
		return;
	irq_flags = process_registry_lock();
	if (!process_list)
		process_list = process;
	process_registry_unlock(irq_flags);
}

pid_t process_get_next_pid(void)
{
	uint64_t irq_flags = process_registry_lock();
	pid_t pid = next_pid++;

	process_registry_unlock(irq_flags);
	return pid;
}

pid_t process_last_assigned_pid(void)
{
	uint64_t irq_flags = process_registry_lock();
	pid_t last = (next_pid > 1) ? (pid_t)(next_pid - 1) : 0;

	process_registry_unlock(irq_flags);
	return last;
}

void process_prepare_pid1_for_init(void)
{
	uint64_t irq_flags;
	process_t *proc;

	irq_flags = process_registry_lock();
	for (proc = process_list; proc; proc = proc->next)
	{
		if (proc->task.pid == 1)
		{
			process_registry_unlock(irq_flags);
			return;
		}
	}
	next_pid = 1;
	process_registry_unlock(irq_flags);
}

int process_remove_from_list(process_t *target)
{
	process_t *scan;
	process_t *prev;
	uint64_t irq_flags;

	if (!target)
		return -EINVAL;
	irq_flags = process_registry_lock();
	prev = NULL;
	for (scan = process_list; scan; scan = scan->next)
	{
		if (scan != target)
		{
			prev = scan;
			continue;
		}
		if (prev)
			prev->next = scan->next;
		else
			process_list = scan->next;
		scan->next = NULL;
		process_registry_unlock(irq_flags);
		return 0;
	}
	process_registry_unlock(irq_flags);
	return -ENOENT;
}
