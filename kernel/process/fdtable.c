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
#include <ir0/fd_get.h>
#include <ir0/devfs.h>
#include <ir0/memfd.h>
#include <ir0/eventfd.h>
#include <ir0/timerfd.h>

int ir0_fd_get(process_t *proc, int fd, ir0_fd_t *out)
{
	files_struct_t *f;
	fd_entry_t *e;

	if (!out)
		return -EINVAL;

	out->files = NULL;
	out->entry = NULL;
	out->fd = -1;

	if (!proc)
		return -ESRCH;
	if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
		return -EBADF;

	f = proc->files;
	if (!files_struct_live(f))
		return -EBADF;
	if (!files_get(f))
		return -EBADF;

	e = &f->fd_table[fd];
	if (!e->in_use)
	{
		files_put(f);
		return -EBADF;
	}

	out->files = f;
	out->entry = e;
	out->fd = fd;
	return 0;
}

void ir0_fd_put(ir0_fd_t *h)
{
	if (!h || !h->files)
		return;
	files_put(h->files);
	h->files = NULL;
	h->entry = NULL;
	h->fd = -1;
}

void process_release_fds(process_t *p, const char *pipe_trace_op)
{
	fd_entry_t *table;
	int i;

	(void)pipe_trace_op;

	if (!p || !files_struct_live(p->files))
		return;

	/*
	 * Shared files_struct (CLONE_FILES): other processes still use the table;
	 * only the last reference closes underlying handles.
	 */
	if (p->files->refcount != 1)
		return;

	table = p->files->fd_table;
	for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
	{
		fd_entry_t *e = &table[i];

		if (!e->in_use)
			continue;

		if (e->is_pipe && e->vfs_file)
		{
			pipe_t *pip = (pipe_t *)e->vfs_file;
			/* pipe_close_end wakes waiters then frees on last ref. */
			pipe_fd_entry_release_refs(pip, e);
			e->vfs_file = NULL;
		}
		else if (e->is_socket && e->vfs_file)
		{
			if (sock_stream_is(e->vfs_file))
				sock_stream_release((struct sock_stream *)e->vfs_file);
			else if (sock_icmp_is(e->vfs_file))
				sock_icmp_release((struct sock_icmp *)e->vfs_file);
			else if (!sock_stream_is_slot(e->vfs_file))
				sock_udp_release((struct sock_udp *)e->vfs_file);
			e->vfs_file = NULL;
		}
		else if (e->is_devfs)
		{
			devfs_node_t *node = fd_entry_devfs_node(e);

			if (e->vfs_file &&
			    devfs_is_ptmx_device(e->dev_device_id))
				devfs_pty_master_release_vfs(e->vfs_file);
			if (e->vfs_file &&
			    devfs_node_wants_text_snap(e->dev_device_id))
			{
				devfs_text_snap_release(
					(devfs_text_snap_t *)e->vfs_file);
				e->vfs_file = NULL;
			}
			if (node)
				devfs_close_node(node);
		}
		else if (e->is_pseudo && e->vfs_file)
		{
			pseudo_fd_bind_t *bind = (pseudo_fd_bind_t *)e->vfs_file;

			if (pseudo_fd_bind_release(bind))
			{
				(void)pseudo_fs_release_ops(
					(const pseudo_fs_ops_t *)bind->ops,
					bind->ctx, bind->dynamic);
				kfree(bind);
			}
			e->vfs_file = NULL;
		}
		else if (e->is_epoll && e->vfs_file)
		{
			epoll_release(e->vfs_file);
			e->vfs_file = NULL;
		}
		else if (e->is_memfd && e->vfs_file)
		{
			ir0_memfd_release((struct ir0_memfd *)e->vfs_file);
			e->vfs_file = NULL;
		}
		else if (e->is_eventfd && e->vfs_file)
		{
			ir0_eventfd_release((struct ir0_eventfd *)e->vfs_file);
			e->vfs_file = NULL;
		}
		else if (e->is_timerfd && e->vfs_file)
		{
			ir0_timerfd_release((struct ir0_timerfd *)e->vfs_file);
			e->vfs_file = NULL;
		}
		else if (e->vfs_file)
		{
			vfs_close((struct vfs_file *)e->vfs_file);
			e->vfs_file = NULL;
		}
		/*
		 * Console stdio (0/1/2) with no backend: fall through and clear.
		 * Redirected stdio must take the branches above (Linux closes all).
		 */

		e->in_use = false;
		e->is_pipe = false;
		e->is_socket = false;
		e->is_pseudo = false;
		e->is_epoll = false;
		e->is_memfd = false;
		e->is_eventfd = false;
		e->is_timerfd = false;
		fd_entry_devfs_unbind(e);
		e->pipe_end = -1;
		e->path[0] = '\0';
		e->flags = 0;
		e->fd_flags = 0;
		e->offset = 0;
	}
}

int process_duplicate_fd_table(process_t *parent, process_t *child)
{
	return process_files_clone(child, parent);
}
