/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fdtable.c
 * Description: Process fd_table init, release on exit/destroy, and fork duplicate.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"

void process_release_fds(process_t *p, const char *pipe_trace_op)
{
	int i;

	if (!p)
		return;
	process_fase50_trace_proc("process_release_fds-begin", p);

	for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
	{
		fd_entry_t *e = &p->fd_table[i];

		if (!e->in_use)
			continue;

		if (e->is_pipe && e->vfs_file)
		{
			pipe_t *pip = (pipe_t *)e->vfs_file;
			int refs_before = pip ? pip->fd_refs : -1;

			if (pipe_trace_op)
			{
				pipe_fase49_fd_trace((uint32_t)p->task.pid, i, pip,
						     e->pipe_end, pip->fd_refs,
						     pipe_trace_op);
			}
			if (DEBUG_FASE50)
			{
				serial_print("[FASE50][FDREL] stage=close_pipe pid=");
				serial_print_hex32((uint32_t)p->task.pid);
				serial_print(" fd=");
				serial_print_hex64((uint64_t)i);
				serial_print(" refs_before=");
				serial_print_hex64((uint64_t)refs_before);
				serial_print(" end=");
				serial_print_hex64((uint64_t)e->pipe_end);
				serial_print("\n");
			}
			pipe_close_end(pip, e->pipe_end);
			pipe_wake_all(pip);
			e->vfs_file = NULL;
		}
		else if (i <= 2)
			goto clear_fd;
		else if (e->is_socket && e->vfs_file)
		{
			sock_udp_release((struct sock_udp *)e->vfs_file);
			e->vfs_file = NULL;
		}
		else if (e->is_devfs)
		{
			devfs_node_t *node = devfs_find_node_by_id(e->dev_device_id);

			if (node)
				devfs_close_node(node);
		}
		else if (e->is_pseudo && e->vfs_file)
		{
			pseudo_fd_bind_t *bind = (pseudo_fd_bind_t *)e->vfs_file;

			if (bind->refs > 0)
				bind->refs--;
			if (bind->refs == 0)
			{
				(void)pseudo_fs_release_ops(
					(const pseudo_fs_ops_t *)bind->ops,
					bind->ctx, bind->dynamic);
				kfree(bind);
			}
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
		e->is_socket = false;
		e->is_devfs = false;
		e->is_pseudo = false;
		e->dev_device_id = 0;
		e->pipe_end = -1;
		e->path[0] = '\0';
		e->flags = 0;
		e->fd_flags = 0;
		e->offset = 0;
	}
	process_fase50_trace_proc("process_release_fds-end", p);
}

int process_duplicate_fd_table(process_t *parent, process_t *child)
{
	int i;

	if (!parent || !child)
		return -EINVAL;

	memcpy(child->fd_table, parent->fd_table, sizeof(child->fd_table));
	for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
	{
		fd_entry_t *e = &child->fd_table[i];

		if (!e->in_use)
			continue;
		if (e->is_pipe && e->vfs_file)
			pipe_acquire_end((pipe_t *)e->vfs_file, e->pipe_end);
		else if (e->is_socket && e->vfs_file)
			sock_udp_acquire((struct sock_udp *)e->vfs_file);
		else if (e->is_devfs)
		{
			devfs_node_t *node = devfs_find_node_by_id(e->dev_device_id);

			if (node)
				node->ref_count++;
		}
		else if (e->is_pseudo && e->vfs_file)
		{
			pseudo_fd_bind_t *bind = (pseudo_fd_bind_t *)e->vfs_file;

			bind->refs++;
		}
		else if (e->vfs_file)
			vfs_file_acquire((struct vfs_file *)e->vfs_file);
	}
	fase_audit_fork_state(child->task.pid, "FILES_CLONED");
	return 0;
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
		process->fd_table[i].is_pipe = false;
		process->fd_table[i].is_socket = false;
		process->fd_table[i].is_devfs = false;
		process->fd_table[i].is_pseudo = false;
		process->fd_table[i].dev_device_id = 0;
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

