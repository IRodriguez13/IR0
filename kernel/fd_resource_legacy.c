/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fd_resource_legacy.c
 * Description: Compatibility providers for existing IR0 descriptor backends.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/fd_resource.h>
#include <ir0/devfs.h>
#include <ir0/pipe_fd.h>
#include <ir0/pseudo_fs.h>
#include <ir0/memfd.h>
#include <ir0/eventfd.h>
#include <ir0/timerfd.h>
#include <ir0/sock_udp.h>
#include <ir0/sock_stream.h>
#include <ir0/sock_icmp.h>
#include <ir0/vfs.h>
#include <ir0/kmem.h>
#include <ir0/errno.h>
#include <kernel/process.h>

extern void epoll_acquire(void *epoll_state);
extern void epoll_release(void *epoll_state);

static int pipe_resource_acquire(fd_entry_t *entry)
{
	pipe_fd_entry_acquire_refs(entry);
	return 0;
}

static void pipe_resource_release(fd_entry_t *entry)
{
	pipe_fd_entry_release_refs((pipe_t *)entry->vfs_file, entry);
}

static int socket_resource_acquire(fd_entry_t *entry)
{
	if (sock_stream_is(entry->vfs_file))
		sock_stream_acquire((struct sock_stream *)entry->vfs_file);
	else if (sock_icmp_is(entry->vfs_file))
		sock_icmp_acquire((struct sock_icmp *)entry->vfs_file);
	else if (!sock_stream_is_slot(entry->vfs_file))
		sock_udp_acquire((struct sock_udp *)entry->vfs_file);
	else
		return -EINVAL;
	return 0;
}

static void socket_resource_release(fd_entry_t *entry)
{
	if (sock_stream_is(entry->vfs_file))
		sock_stream_release((struct sock_stream *)entry->vfs_file);
	else if (sock_icmp_is(entry->vfs_file))
		sock_icmp_release((struct sock_icmp *)entry->vfs_file);
	else if (!sock_stream_is_slot(entry->vfs_file))
		sock_udp_release((struct sock_udp *)entry->vfs_file);
}

static int devfs_resource_acquire(fd_entry_t *entry)
{
	devfs_node_t *node = fd_entry_devfs_node(entry);

	if (node)
		node->ref_count++;
	if (entry->vfs_file && devfs_is_ptmx_device(entry->dev_device_id))
		devfs_pty_master_dup_vfs(entry->vfs_file);
	else if (devfs_is_pts_device(entry->dev_device_id))
		devfs_pty_slave_dup_device(entry->dev_device_id);
	else if (entry->vfs_file && devfs_node_wants_text_snap(entry->dev_device_id))
		devfs_text_snap_acquire((devfs_text_snap_t *)entry->vfs_file);
	return 0;
}

static void devfs_resource_release(fd_entry_t *entry)
{
	devfs_node_t *node = fd_entry_devfs_node(entry);

	if (entry->vfs_file && devfs_is_ptmx_device(entry->dev_device_id))
		devfs_pty_master_release_vfs(entry->vfs_file);
	if (entry->vfs_file && devfs_node_wants_text_snap(entry->dev_device_id))
		devfs_text_snap_release((devfs_text_snap_t *)entry->vfs_file);
	if (node)
		devfs_close_node(node);
}

static int pseudo_resource_acquire(fd_entry_t *entry)
{
	pseudo_fd_bind_acquire((pseudo_fd_bind_t *)entry->vfs_file);
	return 0;
}

static void pseudo_resource_release(fd_entry_t *entry)
{
	pseudo_fd_bind_t *bind = (pseudo_fd_bind_t *)entry->vfs_file;

	if (pseudo_fd_bind_release(bind))
	{
		(void)pseudo_fs_release_ops((const pseudo_fs_ops_t *)bind->ops,
					    bind->ctx, bind->dynamic);
		kfree(bind);
	}
}

#define SIMPLE_RESOURCE_OPS(tag, acquire_fn, release_fn, cast_type) \
	static int tag##_resource_acquire(fd_entry_t *entry) \
	{ acquire_fn((cast_type)entry->vfs_file); return 0; } \
	static void tag##_resource_release(fd_entry_t *entry) \
	{ release_fn((cast_type)entry->vfs_file); }

SIMPLE_RESOURCE_OPS(epoll, epoll_acquire, epoll_release, void *)
SIMPLE_RESOURCE_OPS(memfd, ir0_memfd_acquire, ir0_memfd_release,
		    struct ir0_memfd *)
SIMPLE_RESOURCE_OPS(eventfd, ir0_eventfd_acquire, ir0_eventfd_release,
		    struct ir0_eventfd *)
SIMPLE_RESOURCE_OPS(timerfd, ir0_timerfd_acquire, ir0_timerfd_release,
		    struct ir0_timerfd *)
SIMPLE_RESOURCE_OPS(vfs, vfs_file_acquire, vfs_close, struct vfs_file *)

#define RESOURCE_OPS(tag) \
	static const fd_resource_ops_t tag##_ops = { \
		#tag, tag##_resource_acquire, tag##_resource_release \
	}

RESOURCE_OPS(pipe);
RESOURCE_OPS(socket);
RESOURCE_OPS(devfs);
RESOURCE_OPS(pseudo);
RESOURCE_OPS(epoll);
RESOURCE_OPS(memfd);
RESOURCE_OPS(eventfd);
RESOURCE_OPS(timerfd);
RESOURCE_OPS(vfs);

int fd_resource_bind_legacy(fd_entry_t *entry)
{
	if (!entry)
		return -EINVAL;
	if (entry->is_pipe)
		entry->resource_ops = &pipe_ops;
	else if (entry->is_socket)
		entry->resource_ops = &socket_ops;
	else if (entry->is_devfs)
		entry->resource_ops = &devfs_ops;
	else if (entry->is_pseudo)
		entry->resource_ops = &pseudo_ops;
	else if (entry->is_epoll)
		entry->resource_ops = &epoll_ops;
	else if (entry->is_memfd)
		entry->resource_ops = &memfd_ops;
	else if (entry->is_eventfd)
		entry->resource_ops = &eventfd_ops;
	else if (entry->is_timerfd)
		entry->resource_ops = &timerfd_ops;
	else if (entry->vfs_file)
		entry->resource_ops = &vfs_ops;
	else
		entry->resource_ops = NULL;
	return 0;
}
