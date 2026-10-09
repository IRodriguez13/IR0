/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fd_resource.c
 * Description: Provider-neutral descriptor resource lifetime dispatch.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/fd_resource.h>
#include <ir0/errno.h>
#include <stddef.h>

static int fd_entry_has_resource(const fd_entry_t *entry)
{
	return entry && (entry->vfs_file || entry->is_devfs);
}

int fd_resource_acquire(fd_entry_t *entry)
{
	if (!entry || !entry->in_use)
		return 0;
	if (!entry->resource_ops)
		return fd_entry_has_resource(entry) ? -EINVAL : 0;
	if (!entry->resource_ops->acquire)
		return -EINVAL;
	return entry->resource_ops->acquire(entry);
}

int fd_resource_release(fd_entry_t *entry)
{
	if (!entry || !entry->in_use)
		return 0;
	if (!entry->resource_ops)
		return fd_entry_has_resource(entry) ? -EINVAL : 0;
	if (!entry->resource_ops->release)
		return -EINVAL;
	entry->resource_ops->release(entry);
	entry->vfs_file = NULL;
	return 0;
}

void fd_resource_forget(fd_entry_t *entry)
{
	if (!entry)
		return;
	entry->resource_ops = NULL;
}
