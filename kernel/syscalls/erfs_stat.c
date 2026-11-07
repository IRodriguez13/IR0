/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel/syscalls/erfs_stat.c
 * Description: Early rootfs path stat, faccessat, newfstatat, fstat, readiness probes.
 */

#include "erfs_internal.h"

int early_rootfs_console_ready(void)
{
	struct ir0_early_stat64 st;

	if (!g_ready)
		return 0;
	if (ir0_early_rootfs_path_node("/dev") != IR0_EARLY_ROOTFS_DEV)
		return 0;
	if (ir0_early_rootfs_path_node("/dev/console") != IR0_EARLY_ROOTFS_CONSOLE)
		return 0;
	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_CONSOLE, 0, &st);
	return st.st_mode == IR0_EARLY_ROOTFS_MODE_CHR &&
	       st.st_rdev == IR0_EARLY_ROOTFS_CONSOLE_RDEV;
}

int early_rootfs_memdev_ready(void)
{
	struct ir0_early_stat64 st;

	if (!g_ready)
		return 0;
	if (ir0_early_rootfs_path_node("/dev/null") != IR0_EARLY_ROOTFS_NULL)
		return 0;
	if (ir0_early_rootfs_path_node("/dev/zero") != IR0_EARLY_ROOTFS_ZERO)
		return 0;
	if (ir0_early_rootfs_path_node("/dev/tty") != IR0_EARLY_ROOTFS_TTY)
		return 0;
	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_NULL, 0, &st);
	if (st.st_rdev != IR0_EARLY_ROOTFS_NULL_RDEV)
		return 0;
	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_ZERO, 0, &st);
	if (st.st_rdev != IR0_EARLY_ROOTFS_ZERO_RDEV)
		return 0;
	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_TTY, 0, &st);
	return st.st_mode == IR0_EARLY_ROOTFS_MODE_CHR &&
	       st.st_rdev == IR0_EARLY_ROOTFS_TTY_RDEV;
}

int64_t early_rootfs_faccessat(int dirfd, uint64_t path, int flags)
{
	char pbuf[32];
	enum ir0_early_rootfs_node node;

	(void)flags;
	if (!g_ready)
		return -ENOENT;
	if (dirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(dirfd) < 0)
		return -EBADF;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;
	node = lookup_user_path(pbuf);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -ENOENT;
	if (node == IR0_EARLY_ROOTFS_VDA && !vda_present())
		return -ENOENT;
	{
		uint64_t need = 0;
		int denied;

		if (flags & 4)
			need |= node_is_dir(node) ?
				IR0_EARLY_LANDLOCK_FS_READ_DIR :
				IR0_EARLY_LANDLOCK_FS_READ_FILE;
		if (flags & 2)
			need |= IR0_EARLY_LANDLOCK_FS_WRITE_FILE;
		if (flags & 1)
			need |= IR0_EARLY_LANDLOCK_FS_EXECUTE;
		denied = landlock_allow(pbuf, need);
		if (denied < 0)
			return denied;
	}
	return 0;
}

int64_t early_rootfs_newfstatat(int dirfd, uint64_t path, uint64_t statbuf,
				int flags)
{
	char pbuf[32];
	enum ir0_early_rootfs_node node;
	struct ir0_early_stat64 st;

	if (!g_ready)
		return -ENOENT;
	if (dirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(dirfd) < 0)
		return -EBADF;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;
	if (!join_under_cwd(pbuf, sizeof(pbuf)))
		return -ENAMETOOLONG;
	node = lookup_user_path(pbuf);
	if (node == IR0_EARLY_ROOTFS_SYMLINK &&
	    !(flags & IR0_EARLY_AT_SYMLINK_NOFOLLOW))
	{
		int walked = follow_path(pbuf, sizeof(pbuf));

		if (walked < 0)
			return -ELOOP;
		if (!walked)
			return -ENOENT;
		node = lookup_user_path(pbuf);
	}
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -ENOENT;
	if (node == IR0_EARLY_ROOTFS_VDA && !vda_present())
		return -ENOENT;
	{
		uint64_t size = g_blob_len;
		int fsid;

		if (node == IR0_EARLY_ROOTFS_VDA)
			size = vda_bytes();
		else if (node == IR0_EARLY_ROOTFS_INIT)
			size = g_init_vis;
		else if (node == IR0_EARLY_ROOTFS_FILE)
		{
			fsid = ramtab_body(ramtab_find(pbuf));
			size = (fsid >= 0 && ramtab_table()[fsid].used) ? ramtab_table()[fsid].size :
								0;
		}
		else if (node == IR0_EARLY_ROOTFS_SYMLINK)
		{
			fsid = ramtab_find(pbuf);
			size = (fsid >= 0) ? ramtab_table()[fsid].size : 0;
		}
		else if (node == IR0_EARLY_ROOTFS_UNIX ||
			 node == IR0_EARLY_ROOTFS_FIFO ||
			 node == IR0_EARLY_ROOTFS_USERDIR)
			size = 0;
		ir0_early_rootfs_fill_stat(node, size, &st);
		fsid = ramtab_body(ramtab_find(pbuf));
		if (fsid >= 0 && ramtab_table()[fsid].used)
			st.st_mode = ramtab_table()[fsid].mode;
	}
	if (!g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(statbuf, &st, sizeof(st)) != 0)
		return -EFAULT;
	return 0;
}

int64_t early_rootfs_fstat(int fd, uint64_t statbuf)
{
	enum ir0_early_rootfs_node node;
	struct ir0_early_stat64 st;
	uint64_t size;
	int mid;

	node = node_of_fd(fd);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -EBADF;
	if (node == IR0_EARLY_ROOTFS_VDA)
		size = vda_bytes();
	else if (node == IR0_EARLY_ROOTFS_INIT)
		size = g_init_vis;
	else if (node == IR0_EARLY_ROOTFS_MEMFD)
	{
		mid = pipe_id_of_fd(fd);
		size = (mid >= 0 && mid < MEMFD_MAX && g_memfd[mid].used) ?
			       g_memfd[mid].len :
			       0;
	}
	else if (node == IR0_EARLY_ROOTFS_FILE)
	{
		mid = ramtab_body(pipe_id_of_fd(fd));
		size = (mid >= 0 && mid < RAMTAB_MAX && ramtab_table()[mid].used) ?
			       ramtab_table()[mid].size :
			       0;
	}
	else
		size = g_blob_len;
	ir0_early_rootfs_fill_stat(node, size, &st);
	if (node == IR0_EARLY_ROOTFS_MEMFD && mid >= 0 && mid < MEMFD_MAX &&
	    g_memfd[mid].used)
		st.st_mode = g_memfd[mid].mode;
	if (node == IR0_EARLY_ROOTFS_FILE || node == IR0_EARLY_ROOTFS_FIFO ||
	    node == IR0_EARLY_ROOTFS_USERDIR)
	{
		mid = ramtab_body(pipe_id_of_fd(fd));
		if (mid >= 0 && mid < RAMTAB_MAX && ramtab_table()[mid].used)
			st.st_mode = ramtab_table()[mid].mode;
	}
	if (!g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(statbuf, &st, sizeof(st)) != 0)
		return -EFAULT;
	return 0;
}

int early_rootfs_vda_ready(void)
{
	struct ir0_early_stat64 st;

	if (!g_ready || !vda_present())
		return 0;
	if (ir0_early_rootfs_path_node("/dev/vda") != IR0_EARLY_ROOTFS_VDA)
		return 0;
	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_VDA, vda_bytes(), &st);
	return st.st_mode == IR0_EARLY_ROOTFS_MODE_BLK &&
	       st.st_rdev == IR0_EARLY_ROOTFS_VDA_RDEV &&
	       st.st_size > 0;
}
