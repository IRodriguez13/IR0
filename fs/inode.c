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

static unsigned int icache_bucket(const struct super_block *sb, ino_t ino)
{
	uintptr_t h = ((uintptr_t)sb >> 4) ^ (uintptr_t)ino;

	return (unsigned int)(h % ICACHE_BUCKETS);
}

/* icache_lock held. */
static struct inode *icache_find(struct super_block *sb, ino_t ino)
{
	struct inode *inode;

	for (inode = icache[icache_bucket(sb, ino)]; inode; inode = inode->i_next)
	{
		if (inode->i_sb == sb && inode->i_ino == ino)
			return inode;
	}
	return NULL;
}

/* icache_lock held. */
static void icache_unhash(struct inode *inode)
{
	struct inode **link;

	link = &icache[icache_bucket(inode->i_sb, inode->i_ino)];
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

struct inode *iget(struct super_block *sb, const char *path, const stat_t *st)
{
	struct inode *inode;
	struct inode *fresh;
	unsigned long flags;
	if (!sb || !sb->s_op->iops || !path || !st)
		return NULL;
	fresh = kcalloc(1, sizeof(*fresh));
	if (!fresh)
		return NULL;
	fresh->i_sb = sb;
	fresh->i_ino = st->st_ino;
	fresh->i_mode = st->st_mode & S_IFMT;
	fresh->i_count = 1;
	flags = ir0_spin_lock_irqsave(&icache_lock);
	inode = icache_find(sb, st->st_ino);
	if (inode)
		inode->i_count++;
	ir0_spin_unlock_irqrestore(&icache_lock, flags);
	if (inode)
	{
		kfree(fresh);
		return inode;
	}
	/* Backend per-file state; the fs-op lock keeps the key unclaimed. */
	if (sb->s_op->iops->open_inode &&
	    sb->s_op->iops->open_inode(fresh, path) != 0)
	{
		kfree(fresh);
		return NULL;
	}

	flags = ir0_spin_lock_irqsave(&icache_lock);
	inode = icache_find(sb, st->st_ino);
	if (inode)
	{
		inode->i_count++;
	}
	else
	{
		unsigned int b = icache_bucket(sb, fresh->i_ino);

		fresh->i_next = icache[b];
		icache[b] = fresh;
		sb->s_inodes++;
		inode = fresh;
		fresh = NULL;
	}
	ir0_spin_unlock_irqrestore(&icache_lock, flags);
	if (fresh && sb->s_op->iops->release)
		sb->s_op->iops->release(fresh);
	kfree(fresh);
	return inode;
}

struct inode *ilookup(struct super_block *sb, const char *path)
{
	struct inode *inode;
	unsigned long flags;
	stat_t st;

	if (!sb || !sb->s_op->iops || !path || !sb->s_op->stat ||
	    sb->s_op->stat(path, &st) != 0)
		return NULL;
	flags = ir0_spin_lock_irqsave(&icache_lock);
	inode = icache_find(sb, st.st_ino);
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
	{
		icache_unhash(inode);
		inode->i_sb->s_inodes--;
	}
	ir0_spin_unlock_irqrestore(&icache_lock, flags);
	if (!last)
		return;
	/* Linux evict(): an unlinked file goes with its last reference. */
	{
		const struct vfs_inode_ops *iops = inode->i_sb->s_op->iops;

		vfs_fs_op_acquire();
		if ((inode->i_state & I_ORPHAN) && iops->evict)
			iops->evict(inode);
		if (iops->release)
			iops->release(inode);
		vfs_fs_op_release();
	}
	kfree(inode);
}

struct inode *inode_orphan(struct super_block *sb, ino_t ino)
{
	struct inode *inode;
	unsigned long flags;

	if (!sb || !sb->s_op->iops)
		return NULL;
	flags = ir0_spin_lock_irqsave(&icache_lock);
	inode = icache_find(sb, ino);
	if (inode)
	{
		/* Nameless now: a new file reusing the number is another inode. */
		inode->i_state |= I_ORPHAN;
		icache_unhash(inode);
	}
	ir0_spin_unlock_irqrestore(&icache_lock, flags);
	return inode;
}

/* Backend I/O of @inode without the page cache. fs-op lock held. */
static int inode_raw_read(struct inode *inode, void *buf, size_t count,
			  size_t *done, off_t offset)
{
	struct vfs_ops *fs = inode->i_sb->s_op;

	if (!fs->iops->read)
		return -ENOSYS;
	return fs->iops->read(inode, buf, count, done, offset);
}

static int inode_raw_write(struct inode *inode, const void *buf, size_t count,
			   size_t *done, off_t offset)
{
	struct vfs_ops *fs = inode->i_sb->s_op;

	if (!fs->iops->write)
		return -ENOSYS;
	return fs->iops->write(inode, buf, count, done, offset);
}

static int inode_raw_stat(struct inode *inode, stat_t *st)
{
	struct vfs_ops *fs = inode->i_sb->s_op;

	if (!fs->iops->stat)
		return -ENOSYS;
	return fs->iops->stat(inode, st);
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
	fs = inode->i_sb->s_op;
	vfs_fs_op_acquire();
	if (fs->iops->truncate)
		ret = fs->iops->truncate(inode, length);
	else
		ret = -ENOSYS;
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
