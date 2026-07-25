/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: flock.c
 * Description: Advisory whole-file locks (flock(2)), per open file description.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/flock.h>

#include <fs/vfs.h>
#include <ir0/errno.h>
#include <ir0/fcntl.h>
#include <ir0/ktm/klog.h>
#include <string.h>

#define IR0_FLOCK_MAX 64

/*
 * One record per holding open file description, like Linux: dup(2) and fork(2)
 * share the description and therefore the lock; a second open(2) of the same
 * path is a separate holder and conflicts.
 *
 * Single-CPU kernel, syscall context only: no interrupt handler touches this
 * table, so it needs no irq-save section.
 */
struct flock_holder
{
	struct vfs_file *file;
	char path[VFS_PATH_MAX];
	int exclusive;
};

static struct flock_holder flock_table[IR0_FLOCK_MAX];

static struct flock_holder *flock_find(const struct vfs_file *file)
{
	size_t i;

	for (i = 0; i < IR0_FLOCK_MAX; i++)
	{
		if (flock_table[i].file == file)
			return &flock_table[i];
	}
	return NULL;
}

static struct flock_holder *flock_alloc(void)
{
	size_t i;

	for (i = 0; i < IR0_FLOCK_MAX; i++)
	{
		if (!flock_table[i].file)
			return &flock_table[i];
	}
	return NULL;
}

static int flock_conflicts(const struct vfs_file *file, const char *path,
			   int want_exclusive)
{
	size_t i;

	for (i = 0; i < IR0_FLOCK_MAX; i++)
	{
		const struct flock_holder *h = &flock_table[i];

		if (!h->file || h->file == file)
			continue;
		if (strcmp(h->path, path) != 0)
			continue;
		if (want_exclusive || h->exclusive)
			return 1;
	}
	return 0;
}

int ir0_flock_apply(struct vfs_file *file, int operation)
{
	struct flock_holder *own;
	int want_exclusive;

	if (!file)
		return -EBADF;

	if (operation & LOCK_UN)
	{
		if (operation & (LOCK_SH | LOCK_EX))
			return -EINVAL;
		ir0_flock_release_file(file);
		return 0;
	}

	if (!(operation & (LOCK_SH | LOCK_EX)))
		return -EINVAL;
	if ((operation & LOCK_SH) && (operation & LOCK_EX))
		return -EINVAL;

	want_exclusive = (operation & LOCK_EX) != 0;

	if (flock_conflicts(file, file->path, want_exclusive))
	{
		if (operation & LOCK_NB)
			return -EWOULDBLOCK;
		/*
		 * Blocking flock needs a per-file wait queue; until then the
		 * caller is told the lock is unavailable rather than being
		 * handed a lock somebody else holds.
		 */
		klog_notice_fmt("FLOCK",
				"blocking request on %s not supported yet (ENOLCK)",
				file->path);
		return -ENOLCK;
	}

	own = flock_find(file);
	if (!own)
	{
		own = flock_alloc();
		if (!own)
			return -ENOLCK;
		own->file = file;
		strncpy(own->path, file->path, sizeof(own->path) - 1);
		own->path[sizeof(own->path) - 1] = '\0';
	}

	/* Re-locking the same description converts the mode in place. */
	own->exclusive = want_exclusive;
	return 0;
}

void ir0_flock_release_file(struct vfs_file *file)
{
	struct flock_holder *own;

	if (!file)
		return;

	own = flock_find(file);
	if (!own)
		return;

	own->file = NULL;
	own->exclusive = 0;
	own->path[0] = '\0';
}
