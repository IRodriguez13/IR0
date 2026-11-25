/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: inode.c
 * Description: Inode cache, inode lifetime and inode I/O (Linux
 *              fs/inode.c iget_locked/iput/evict).
 */

#include <ir0/errno.h>
#include <ir0/filemap.h>
#include <ir0/inode.h>
#include <ir0/kmem.h>
#include <ir0/spinlock.h>
#include <string.h>
#include "vfs_internal.h"

#define ICACHE_BUCKETS 64

/*
 * The cached inodes. icache_lock guards the chains, i_count and i_state;
 * it is never held across backend I/O or together with filemap_lock.
 */
static struct inode *icache[ICACHE_BUCKETS];
static ir0_spinlock_t icache_lock;

static unsigned int icache_hash_path(const char *path)
{
	unsigned int h = 5381;

	while (*path)
		h = h * 33u + (unsigned char)*path++;
	return h;
}

static unsigned int icache_bucket(const struct vfs_ops *fs, ino_t ino,
				  const char *path)
{
	uintptr_t h = (uintptr_t)fs >> 4;

	h ^= fs->iops ? (uintptr_t)ino : icache_hash_path(path);
	return (unsigned int)(h % ICACHE_BUCKETS);
}

static int inode_matches(const struct inode *inode, const struct vfs_ops *fs,
			 ino_t ino, const char *path)
{
	if (inode->i_fs != fs)
		return 0;
	if (fs->iops)
		return inode->i_ino == ino;
	return inode->i_path[0] && strcmp(inode->i_path, path) == 0;
}

/* icache_lock held. */
static struct inode *icache_find(struct vfs_ops *fs, ino_t ino,
				 const char *path)
{
	struct inode *inode;

	for (inode = icache[icache_bucket(fs, ino, path)]; inode;
	     inode = inode->i_next)
	{
		if (inode_matches(inode, fs, ino, path))
			return inode;
	}
	return NULL;
}

/* icache_lock held. */
static void icache_unhash(struct inode *inode)
{
	struct inode **link;

	link = &icache[icache_bucket(inode->i_fs, inode->i_ino, inode->i_path)];
	for (; *link; link = &(*link)->i_next)
	{
		if (*link == inode)
		{
			*link = inode->i_next;
			inode->i_next = NULL;
			return;
		}
	}
}

struct inode *iget(struct vfs_ops *fs, const char *path, const stat_t *st)
{
	struct inode *inode;
	struct inode *fresh;
	unsigned long flags;
	size_t len;

	if (!fs || !path || !st)
		return NULL;
	len = strlen(path);
	if (len >= sizeof(fresh->i_path))
		return NULL;
	fresh = kcalloc(1, sizeof(*fresh));
	if (!fresh)
		return NULL;
	fresh->i_fs = fs;
	fresh->i_ino = st->st_ino;
	fresh->i_mode = st->st_mode & S_IFMT;
	fresh->i_count = 1;
	if (!fs->iops)
		memcpy(fresh->i_path, path, len + 1);

	flags = ir0_spin_lock_irqsave(&icache_lock);
	inode = icache_find(fs, st->st_ino, path);
	if (inode)
	{
		inode->i_count++;
	}
	else
	{
		unsigned int b = icache_bucket(fs, fresh->i_ino, fresh->i_path);

		fresh->i_next = icache[b];
		icache[b] = fresh;
		inode = fresh;
		fresh = NULL;
	}
	ir0_spin_unlock_irqrestore(&icache_lock, flags);
	kfree(fresh);
	return inode;
}

struct inode *ilookup(struct vfs_ops *fs, const char *path)
{
	struct inode *inode;
	unsigned long flags;
	stat_t st;

	if (!fs || !path)
		return NULL;
	memset(&st, 0, sizeof(st));
	if (fs->iops && (!fs->stat || fs->stat(path, &st) != 0))
		return NULL;
	flags = ir0_spin_lock_irqsave(&icache_lock);
	inode = icache_find(fs, st.st_ino, path);
	if (inode)
		inode->i_count++;
	ir0_spin_unlock_irqrestore(&icache_lock, flags);
	return inode;
}

void ihold(struct inode *inode)
{
	unsigned long flags;

	if (!inode)
		return;
	flags = ir0_spin_lock_irqsave(&icache_lock);
	inode->i_count++;
	ir0_spin_unlock_irqrestore(&icache_lock, flags);
}

void iput(struct inode *inode)
{
	unsigned long flags;
	int last;

	if (!inode)
		return;
	flags = ir0_spin_lock_irqsave(&icache_lock);
	last = --inode->i_count == 0;
	if (last)
		icache_unhash(inode);
	ir0_spin_unlock_irqrestore(&icache_lock, flags);
	if (!last)
		return;
	/* Linux evict(): an unlinked file goes with its last reference. */
	if ((inode->i_state & I_ORPHAN) && inode->i_fs->iops &&
	    inode->i_fs->iops->evict)
	{
		vfs_fs_op_acquire();
		inode->i_fs->iops->evict(inode->i_ino);
		vfs_fs_op_release();
	}
	kfree(inode);
}

int inode_orphan(const struct vfs_ops *fs, ino_t ino)
{
	struct inode *inode;
	unsigned long flags;

	if (!fs || !fs->iops)
		return 0;
	flags = ir0_spin_lock_irqsave(&icache_lock);
	inode = icache_find((struct vfs_ops *)fs, ino, "");
	if (inode)
		inode->i_state |= I_ORPHAN;
	ir0_spin_unlock_irqrestore(&icache_lock, flags);
	return inode != NULL;
}

void inode_moved(struct vfs_ops *fs, const char *oldpath, const char *newpath)
{
	struct inode *gone;
	struct inode *inode;
	unsigned long flags;

	if (!fs || fs->iops || !oldpath)
		return;
	if (newpath && (strcmp(oldpath, newpath) == 0 ||
			strlen(newpath) >= sizeof(inode->i_path)))
		return;
	flags = ir0_spin_lock_irqsave(&icache_lock);
	/* A rename over a name takes that name from the file it named. */
	gone = newpath ? icache_find(fs, 0, newpath) : NULL;
	if (gone)
	{
		icache_unhash(gone);
		gone->i_path[0] = '\0';
	}
	inode = icache_find(fs, 0, oldpath);
	if (inode)
	{
		icache_unhash(inode);
		if (newpath)
		{
			unsigned int b;

			memcpy(inode->i_path, newpath, strlen(newpath) + 1);
			b = icache_bucket(fs, 0, inode->i_path);
			inode->i_next = icache[b];
			icache[b] = inode;
		}
		else
		{
			inode->i_path[0] = '\0';
		}
	}
	ir0_spin_unlock_irqrestore(&icache_lock, flags);
}

/* Backend I/O of @inode without the page cache. fs-op lock held. */
static int inode_raw_read(struct inode *inode, void *buf, size_t count,
			  size_t *done, off_t offset)
{
	struct vfs_ops *fs = inode->i_fs;

	if (fs->iops && fs->iops->read)
		return fs->iops->read(inode->i_ino, buf, count, done, offset);
	if (!fs->read)
		return -ENOSYS;
	if (!inode->i_path[0])
		return -ENOENT;
	return fs->read(inode->i_path, buf, count, done, offset);
}

static int inode_raw_write(struct inode *inode, const void *buf, size_t count,
			   size_t *done, off_t offset)
{
	struct vfs_ops *fs = inode->i_fs;

	if (fs->iops && fs->iops->write)
		return fs->iops->write(inode->i_ino, buf, count, done, offset);
	if (!fs->write)
		return -ENOSYS;
	if (!inode->i_path[0])
		return -ENOENT;
	return fs->write(inode->i_path, buf, count, done, offset);
}

static int inode_raw_stat(struct inode *inode, stat_t *st)
{
	struct vfs_ops *fs = inode->i_fs;

	if (fs->iops && fs->iops->stat)
		return fs->iops->stat(inode->i_ino, st);
	if (!fs->stat)
		return -ENOSYS;
	if (!inode->i_path[0])
		return -ENOENT;
	return fs->stat(inode->i_path, st);
}

int inode_stat(struct inode *inode, stat_t *st)
{
	int ret;

	if (!inode || !st)
		return -EINVAL;
	vfs_fs_op_acquire();
	ret = inode_raw_stat(inode, st);
	vfs_fs_op_release();
	return ret;
}

int inode_read(struct inode *inode, void *buf, size_t count, size_t *done,
	       off_t offset)
{
	int ret;

	if (!inode)
		return -EINVAL;
	vfs_fs_op_acquire();
	ret = inode_raw_read(inode, buf, count, done, offset);
	if (ret == 0 && done)
		filemap_sync_read(inode, buf, *done, offset);
	vfs_fs_op_release();
	return ret;
}

int inode_write(struct inode *inode, const void *buf, size_t count,
		size_t *done, off_t offset)
{
	int ret;

	if (!inode)
		return -EINVAL;
	vfs_fs_op_acquire();
	ret = inode_raw_write(inode, buf, count, done, offset);
	if (ret == 0 && done)
		filemap_sync_write(inode, buf, *done, offset);
	vfs_fs_op_release();
	return ret;
}

int inode_truncate(struct inode *inode, size_t length)
{
	struct vfs_ops *fs;
	int ret;

	if (!inode)
		return -EINVAL;
	fs = inode->i_fs;
	vfs_fs_op_acquire();
	if (fs->iops && fs->iops->truncate)
		ret = fs->iops->truncate(inode->i_ino, length);
	else if (!fs->truncate)
		ret = -ENOSYS;
	else if (!inode->i_path[0])
		ret = -ENOENT;
	else
		ret = fs->truncate(inode->i_path, length);
	if (ret == 0)
		filemap_sync_truncate(inode, length);
	vfs_fs_op_release();
	return ret;
}

int vfs_pagecache_read(struct inode *inode, void *buf, size_t count,
		       off_t offset, size_t *done)
{
	int ret;

	vfs_fs_op_acquire();
	ret = inode_raw_read(inode, buf, count, done, offset);
	vfs_fs_op_release();
	return ret;
}

int vfs_pagecache_write(struct inode *inode, const void *buf, size_t count,
			off_t offset, size_t *done)
{
	int ret;

	vfs_fs_op_acquire();
	ret = inode_raw_write(inode, buf, count, done, offset);
	vfs_fs_op_release();
	return ret;
}

int vfs_pagecache_size(struct inode *inode, size_t *size)
{
	stat_t st;
	int ret;

	vfs_fs_op_acquire();
	ret = inode_raw_stat(inode, &st);
	vfs_fs_op_release();
	if (ret != 0)
		return ret;
	if (!S_ISREG(st.st_mode) || st.st_size < 0)
		return -ENODEV;
	*size = (size_t)st.st_size;
	return 0;
}
