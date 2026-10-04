/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: files_struct.c
 * Description: files_struct refcount and process bind/share/clone helpers.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <ir0/files_struct.h>
#include <ir0/errno.h>
#include <ir0/memfd.h>
#include <ir0/eventfd.h>
#include <ir0/timerfd.h>
#include <ir0/devfs.h>
#include <string.h>

static int process_files_acquire_entries(files_struct_t *f)
{
	int i;

	if (!f)
		return -EINVAL;

	for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
	{
		fd_entry_t *e = &f->fd_table[i];

		if (!e->in_use)
			continue;
		if (e->is_pipe && e->vfs_file)
			pipe_fd_entry_acquire_refs(e);
		else if (e->is_socket && e->vfs_file)
		{
			if (sock_stream_is(e->vfs_file))
				sock_stream_acquire((struct sock_stream *)e->vfs_file);
			else if (sock_icmp_is(e->vfs_file))
				sock_icmp_acquire((struct sock_icmp *)e->vfs_file);
			else if (!sock_stream_is_slot(e->vfs_file))
				sock_udp_acquire((struct sock_udp *)e->vfs_file);
		}
		else if (e->is_devfs)
		{
			devfs_node_t *node = fd_entry_devfs_node(e);

			if (node)
				node->ref_count++;
			if (e->vfs_file &&
			    devfs_is_ptmx_device(e->dev_device_id))
				devfs_pty_master_dup_vfs(e->vfs_file);
			else if (devfs_is_pts_device(e->dev_device_id))
				devfs_pty_slave_dup_device(e->dev_device_id);
			else if (e->vfs_file &&
			    devfs_node_wants_text_snap(e->dev_device_id))
				devfs_text_snap_acquire(
					(devfs_text_snap_t *)e->vfs_file);
		}
		else if (e->is_pseudo && e->vfs_file)
		{
			pseudo_fd_bind_t *bind = (pseudo_fd_bind_t *)e->vfs_file;

			pseudo_fd_bind_acquire(bind);
		}
		else if (e->is_epoll && e->vfs_file)
			epoll_acquire(e->vfs_file);
		else if (e->is_memfd && e->vfs_file)
			ir0_memfd_acquire((struct ir0_memfd *)e->vfs_file);
		else if (e->is_eventfd && e->vfs_file)
			ir0_eventfd_acquire((struct ir0_eventfd *)e->vfs_file);
		else if (e->is_timerfd && e->vfs_file)
			ir0_timerfd_acquire((struct ir0_timerfd *)e->vfs_file);
		else if (e->vfs_file)
			vfs_file_acquire((struct vfs_file *)e->vfs_file);
	}
	return 0;
}

int process_files_share(process_t *child, process_t *parent)
{
	files_struct_t *f;

	if (!child || !parent)
		return -EINVAL;

	f = parent->files;
	if (!files_struct_live(f))
		return -EINVAL;

	if (!files_get(f))
		return -EINVAL;
	process_files_bind(child, f);
	return 0;
}

int process_files_clone(process_t *child, process_t *parent)
{
	files_struct_t *nf;

	if (!child || !parent || !files_struct_live(parent->files))
		return -EINVAL;

	nf = files_create();
	if (!nf)
		return -ENOMEM;

	memcpy(nf->fd_table, parent->files->fd_table, sizeof(nf->fd_table));
	if (process_files_acquire_entries(nf) != 0)
	{
		files_put(nf);
		return -ENOMEM;
	}

	process_files_bind(child, nf);
	return 0;
}
