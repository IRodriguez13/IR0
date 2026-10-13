/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Parent/child relationship policy shared by every ISA.
 */

#include "process_internal.h"

void process_reparent_children(process_t *dying_parent)
{
	process_t *init;

	if (!dying_parent)
		return;

	init = process_find_by_pid(1);
	if (!init || init == dying_parent)
		init = NULL;

	/* No suitable init (including PID 1 exit): detach instead. */
	(void)process_lifecycle_reparent(process_list, dying_parent, init);
}

void process_notify_parent_of_exit(process_t *dying)
{
	process_t *parent = NULL;
	int parent_state_before = -1;

	if (!dying)
		return;

	if (dying->ppid > 0) {
		parent = process_find_by_pid(dying->ppid);
		if (parent)
			parent_state_before = parent->state;
		if (parent && parent->state != PROCESS_ZOMBIE) {
			send_signal(parent->task.pid, SIGCHLD);
			if (parent->state == PROCESS_BLOCKED ||
			    process_wait_blocked(parent))
				process_wait_wake_blocked_parent(parent, dying);
		} else {
			/* A missing/dead parent delegates reaping to init. */
			dying->ppid = 1;
			parent = process_find_by_pid(1);
			if (parent) {
				if (parent_state_before == -1)
					parent_state_before = parent->state;
				send_signal(parent->task.pid, SIGCHLD);
				if (parent->state == PROCESS_BLOCKED ||
				    process_wait_blocked(parent))
					process_wait_wake_blocked_parent(parent, dying);
			} else {
				dying->ppid = 0;
			}
		}
	}

	wait_exit_audit_process_exit(dying, parent, parent_state_before);
}
