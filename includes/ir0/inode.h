/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: inode.h
 * Description: In-memory inodes and the inode cache (Linux fs/inode.c).
 *              Design: Documentation/ai_driven_dev/vfs-inodes.md.
 */

#pragma once

#include <ir0/stat.h>
#include <ir0/types.h>
#include <ir0/vfs_backend.h>
#include <ir0/vfs_file.h>
#include <stddef.h>

struct filemap;

/*
 * One mounted filesystem (Linux super_block). Bind mounts of it share it.
 * Backend state hangs off s_fs_info (vfs_fstype.fill_super sets it).
 * icache_lock guards s_inodes; the mount table lock guards s_count.
 */
struct super_block
{
	struct vfs_ops *s_op;
	void *s_fs_info;
	int s_count;  /* mount entries using it */
	int s_inodes; /* cached inodes: umount is EBUSY while any is held */
};

/* i_state: the last name is gone; the last iput() evicts the file. */
#define I_ORPHAN 1u

/*
 * One file of one filesystem with inode operations (vfs_ops.iops), named
 * by (i_sb, i_ino). The backend stays the owner of the metadata.
 *
 * icache_lock (fs/inode.c) guards i_count, i_state and the hash; the fs-op
 * lock guards the backend data and i_private; i_mapping belongs to
 * mm/filemap, which holds one reference while it is set.
 */
struct inode
{
	struct super_block *i_sb;
	ino_t i_ino;
	mode_t i_mode; /* file type, fixed for the inode's life */
	int i_count;
	unsigned int i_state;
	struct filemap *i_mapping;
	void *i_private; /* backend per-file state (iops->open_inode) */
	struct inode *i_next;
};

/*
 * The inode of the file @st describes on @sb, which has inode operations,
 * with one more reference, or NULL. A new inode gets its backend state
 * from iops->open_inode for backend @path. Called under the fs-op lock
 * that resolved @st.
 */
struct inode *iget(struct super_block *sb, const char *path, const stat_t *st);
/* The cached inode of @path with one more reference, or NULL. */
struct inode *ilookup(struct super_block *sb, const char *path);
void ihold(struct inode *inode);
void iput(struct inode *inode);

/*
 * Backend unlink: the link count of file @ino of @sb reached 0. Returns the
 * inode when the VFS still holds it: it leaves the cache, and the backend
 * keeps the file and its blocks until iops->evict(inode). Returns NULL when
 * the backend frees it now. Called under the fs-op lock, which keeps the
 * returned inode alive until it is released.
 */
struct inode *inode_orphan(struct super_block *sb, ino_t ino);

/*
 * File I/O through an inode (fs-op lock taken inside). They keep the
 * page cache coherent the way the path operations do.
 */
int inode_stat(struct inode *inode, stat_t *st);
int inode_read(struct inode *inode, void *buf, size_t count, size_t *done,
	       off_t offset);
int inode_write(struct inode *inode, const void *buf, size_t count,
		size_t *done, off_t offset);
int inode_truncate(struct inode *inode, size_t length);
