/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fs/minix_meta.c
 * Description: MINIX metadata operations: stat/statfs/chmod/chown/utimens.
 */

#include "minix_fs_internal.h"

int minix_fs_statfs(struct ir0_statfs *buf)
{
	uint32_t first;
	uint32_t limit;
	uint32_t z;
	uint32_t free_z = 0;
	uint32_t free_i = 0;
	uint32_t i;

	if (!buf)
		return -EINVAL;
	if (!minix_fs.initialized || !minix_fs.zone_bitmap)
		return -ENODEV;

	memset(buf, 0, sizeof(*buf));
	first = minix_fs.superblock.s_firstdatazone;
	limit = minix_fs.superblock.s_nzones;
	if (limit > MINIX_MAX_ZONES)
		limit = MINIX_MAX_ZONES;
	if (first >= limit)
		return -EINVAL;

	for (z = first; z < limit; z++)
	{
		if (minix_is_zone_free(z))
			free_z++;
	}

	/* Inode bitmap polarity is opposite of zones: bit 1 = used. */
	for (i = 1; i <= minix_fs.superblock.s_ninodes && i < MINIX_MAX_INODES; i++)
	{
		uint32_t byte = i / 8;
		uint32_t bit = i % 8;

		if ((minix_fs.inode_bitmap[byte] & (1U << bit)) == 0)
			free_i++;
	}

	buf->f_type = IR0_MINIX_SUPER_MAGIC;
	buf->f_bsize = MINIX_BLOCK_SIZE;
	buf->f_frsize = MINIX_BLOCK_SIZE;
	buf->f_blocks = (unsigned long)(limit - first);
	buf->f_bfree = (unsigned long)free_z;
	buf->f_bavail = (unsigned long)free_z;
	buf->f_files = (unsigned long)minix_fs.superblock.s_ninodes;
	buf->f_ffree = (unsigned long)free_i;
	buf->f_namelen = MINIX_NAME_LEN;
	buf->f_flags = 0;
	return 0;
}

int minix_fs_stat(const char *pathname, stat_t *buf)
{
  minix_inode_t inode;
  uint16_t inode_num;
  int rc;

  if (!minix_fs.initialized || !pathname || !buf)
  {
    log_debug_fmt("MINIX", "stat('%s') rejected: init=%d", pathname ? pathname : "(null)",
                  minix_fs.initialized);
    if (!minix_fs.initialized)
      return -ENODEV;
    return -EINVAL;
  }

  kmemset(buf, 0, sizeof(*buf));
  rc = minix_lookup(pathname, &inode, &inode_num);
  if (rc != 0)
  {
    log_debug_fmt("MINIX", "stat('%s') inode not found", pathname);
    return rc;
  }

  if ((inode.i_mode & MINIX_IFMT) == 0)
  {
    log_debug_fmt("MINIX", "stat('%s') invalid mode 0x%x (stale)", pathname,
                  inode.i_mode);
    return -ENOENT;
  }

  buf->st_dev = 0;
  buf->st_ino = inode_num;
  buf->st_nlink = inode.i_nlinks;
  buf->st_uid = inode.i_uid;
  buf->st_gid = inode.i_gid;
  buf->st_size = inode.i_size;
  buf->st_blksize = MINIX_BLOCK_SIZE;
  buf->st_blocks = (blkcnt_t)(((uint64_t)inode.i_size + 511ULL) / 512ULL);
  buf->st_atime = inode.i_time;
  buf->st_mtime = inode.i_time;
  buf->st_ctime = inode.i_time;
  buf->st_mode = inode.i_mode;

  if (strcmp(pathname, "/") == 0 && klog_trace_enabled(KLOG_TRACE_VFS_STAT))
    klog_trace_fmt("MINIX", "stat('%s') OK ino=%u mode=0x%x", pathname, inode_num, buf->st_mode);
  return 0;
}

/*
 * minix_fs_chown - Change file owner and group
 * @path: Path to file
 * @owner: New owner UID, or (uid_t)-1 to leave unchanged
 * @group: New group GID, or (gid_t)-1 to leave unchanged
 * Returns: 0 on success, negative on error
 */
int minix_fs_chown(const char *path, uid_t owner, gid_t group)
{
  minix_inode_t inode_copy;
  uint16_t inode_num;
  int rc;

  if (!ir0_current_cred())
    return -ESRCH;

  rc = minix_lookup(path, &inode_copy, &inode_num);
  if (rc != 0)
    return rc;

  if (!ir0_cred_is_root())
    return -EPERM;

  if (owner != (uid_t)-1)
    inode_copy.i_uid = (uint16_t)owner;
  if (group != (gid_t)-1)
    inode_copy.i_gid = (uint8_t)group;

  return minix_fs_write_inode(inode_num, &inode_copy);
}

/*
 * minix_fs_utimens - utimensat(2) backend.
 *
 * MINIX v1 inodes keep a single timestamp, so the modification time wins and
 * stat() reports it for atime, mtime and ctime alike.
 */
int minix_fs_utimens(const char *path, const struct timespec times[2])
{
  const struct ir0_task_cred *cr = ir0_current_cred();
  minix_inode_t inode_copy;
  uint16_t inode_num;
  int rc;

  if (!path)
    return -EINVAL;
  if (!cr)
    return -ESRCH;

  rc = minix_lookup(path, &inode_copy, &inode_num);
  if (rc != 0)
    return rc;

  if (!ir0_cred_is_root() && cr->euid != inode_copy.i_uid)
    return -EPERM;

  if (times)
    inode_copy.i_time = (uint32_t)times[1].tv_sec;
  else
    inode_copy.i_time = minix_now_seconds();

  return minix_fs_write_inode(inode_num, &inode_copy);
}

/*
 * minix_fs_chmod - VFS chmod: keep inode type bits, apply new permission mask.
 */
int minix_fs_chmod(const char *path, mode_t mode)
{
	minix_inode_t inode;
	uint16_t inode_num;
	int rc;

	if (!ir0_current_cred())
		return -ESRCH;

	rc = minix_lookup(path, &inode, &inode_num);
	if (rc != 0)
		return rc;

	{
		const struct ir0_task_cred *cr = ir0_current_cred();

		if (!ir0_cred_is_root() && cr->euid != inode.i_uid)
			return -EPERM;
	}

	inode.i_mode = (uint16_t)((inode.i_mode & (uint16_t)~07777) |
				  ((uint16_t)mode & 07777));

	return minix_fs_write_inode(inode_num, &inode);
}
