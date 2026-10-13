/* SPDX-License-Identifier: GPL-3.0-only */
/** IR0 Kernel — ISA-neutral process lifecycle transitions. */

#include <kernel/process.h>

void process_lifecycle_publish_exit(process_t *process, int code)
{
	if (!process)
		return;
	process_mark_zombie(process);
	process->exit_code = process->exit_signal == 0 ? code : 0;
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
