/* SPDX-License-Identifier: GPL-3.0-only */
/** IR0 Kernel — ISA-neutral process lifecycle transitions. */

#include <kernel/process.h>
#include <ir0/errno.h>
#include <ir0/oops.h>
#include <string.h>

/*
 * Every exit (return, exit_group, fatal signal) publishes here. Nothing can
 * adopt the orphans of init, so its death stops the system as on Linux
 * (find_child_reaper), reporting the wait status it would have produced.
 */
void process_lifecycle_publish_exit(process_t *process, int code)
{
	char msg[48];

	if (!process)
		return;
	process_mark_zombie(process);
	process->exit_code = process->exit_signal == 0 ? code : 0;
	if (process->task.pid != 1)
		return;
	snprintf(msg, sizeof(msg), "Attempted to kill init! exitcode=0x%08x",
		 (unsigned int)process_lifecycle_wait_status(process));
	panic(msg);
}

int process_lifecycle_wait_status(const process_t *child)
{
	if (!child)
		return 0;
	if (child->exit_signal > 0)
		return child->exit_signal & 0x7f;
	return (child->exit_code & 0xff) << 8;
}

int process_lifecycle_child_matches(const process_t *parent,
				    const process_t *child, pid_t wait_pid)
{
	if (!parent || !child || child->ppid != parent->task.pid)
		return 0;
	if (wait_pid == (pid_t)-1)
		return 1;
	if (wait_pid > 0)
		return child->task.pid == wait_pid;
	if (wait_pid == 0)
		return child->pgid == parent->pgid;
	return child->pgid == (pid_t)(-wait_pid);
}

void process_lifecycle_publish_reaped(process_t *process)
{
	process_mark_dead(process);
}

int process_lifecycle_prepare_reap(const process_t *process)
{
	if (!process)
		return -EINVAL;
	if (process->lifecycle == PROCESS_LIFECYCLE_DEAD)
		return -EALREADY;
	if (!process_is_zombie(process))
		return -EBUSY;
	return 0;
}

size_t process_lifecycle_reparent(process_t *head, process_t *dying,
				  process_t *adopter)
{
	process_t *child;
	size_t reparented = 0;
	pid_t new_parent;

	if (!dying)
		return 0;
	if (adopter == dying)
		adopter = NULL;
	new_parent = adopter ? adopter->task.pid : 0;

	for (child = head; child; child = child->next) {
		if (child == dying || child == adopter ||
		    child->ppid != dying->task.pid)
			continue;
		child->ppid = new_parent;
		reparented++;
	}
	return reparented;
}
