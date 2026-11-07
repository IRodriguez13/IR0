/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel/syscalls/erfs_path.c
 * Description: staged rootfs path operations — chroot, truncate, flock,
 *              utimens, mkdir/mknod/unlink/rename.
 */

#include "erfs_internal.h"

int64_t early_rootfs_chroot(uint64_t path)
{
	char pbuf[32];
	enum ir0_early_rootfs_node node;

	if (!g_ready)
		return -ENOENT;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;
	node = lookup_user_path(pbuf);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -ENOENT;
	if (!node_is_dir(node))
		return -ENOTDIR;
	g_chroot = node;
	{
		unsigned i;

		for (i = 0; pbuf[i] != '\0' && i + 1 < sizeof(g_chroot_path); i++)
			g_chroot_path[i] = pbuf[i];
		g_chroot_path[i] = '\0';
	}
	early_publish_root();
	return 0;
}

int64_t early_rootfs_truncate(uint64_t path, uint64_t length)
{
	char pbuf[32];
	enum ir0_early_rootfs_node node;

	if (!g_ready)
		return -ENOENT;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;
	node = lookup_user_path(pbuf);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -ENOENT;
	{
		int denied = landlock_allow(pbuf, IR0_EARLY_LANDLOCK_FS_TRUNCATE);

		if (denied < 0)
			return denied;
	}
	if (node_is_dir(node))
		return -EISDIR;
	if (node == IR0_EARLY_ROOTFS_FILE)
	{
		int fsid = ramtab_find(pbuf);

		if (fsid < 0)
			return -ENOENT;
		if (!ramtab_table()[fsid].writable)
			return -EROFS;
		if (length > RAMTAB_FILE_CAP)
			return -EFBIG;
		if ((uint32_t)length > ramtab_table()[fsid].size)
		{
			uint32_t i;

			for (i = ramtab_table()[fsid].size; i < (uint32_t)length; i++)
				ramtab_table()[fsid].own[i] = 0;
		}
		ramtab_table()[fsid].size = (uint32_t)length;
		ramtab_table()[fsid].data = ramtab_table()[fsid].own;
		return 0;
	}
	if (node != IR0_EARLY_ROOTFS_INIT)
		return -EINVAL;
	g_init_vis = length < g_blob_len ? length : g_blob_len;
	return 0;
}

int64_t early_rootfs_ftruncate(int fd, uint64_t length)
{
	enum ir0_early_rootfs_node node = node_of_fd(fd);

	if (node == IR0_EARLY_ROOTFS_NONE)
		return -EBADF;
	{
		char pbuf[RAMTAB_NAME];
		int denied;

		if (path_of_fd(fd, pbuf, sizeof(pbuf)) == 0)
		{
			denied = landlock_allow(pbuf,
						IR0_EARLY_LANDLOCK_FS_TRUNCATE);
			if (denied < 0)
				return denied;
		}
	}
	if (node_is_dir(node))
		return -EISDIR;
	if (node == IR0_EARLY_ROOTFS_PIPE_R || node == IR0_EARLY_ROOTFS_PIPE_W ||
	    node == IR0_EARLY_ROOTFS_SOCK || node == IR0_EARLY_ROOTFS_EVENTFD ||
	    node == IR0_EARLY_ROOTFS_TIMERFD || node == IR0_EARLY_ROOTFS_EPOLL)
		return -ESPIPE;
	if (node == IR0_EARLY_ROOTFS_MEMFD)
	{
		int mid = pipe_id_of_fd(fd);
		uint32_t n;
		uint32_t i;

		if (mid < 0 || mid >= MEMFD_MAX || !g_memfd[mid].used)
			return -EBADF;
		if (length > MEMFD_CAP)
			return -EINVAL;
		n = (uint32_t)length;
		if (n > g_memfd[mid].len)
		{
			for (i = g_memfd[mid].len; i < n; i++)
				g_memfd[mid].data[i] = 0;
		}
		g_memfd[mid].len = n;
		return 0;
	}
	if (node == IR0_EARLY_ROOTFS_FILE)
	{
		int fsid = pipe_id_of_fd(fd);
		uint32_t n;
		uint32_t i;

		if (fsid < 0 || fsid >= RAMTAB_MAX || !ramtab_table()[fsid].used)
			return -EBADF;
		if (!ramtab_table()[fsid].writable)
			return -EROFS;
		if (length > RAMTAB_FILE_CAP)
			return -EFBIG;
		n = (uint32_t)length;
		if (n > ramtab_table()[fsid].size)
		{
			for (i = ramtab_table()[fsid].size; i < n; i++)
				ramtab_table()[fsid].own[i] = 0;
		}
		ramtab_table()[fsid].size = n;
		ramtab_table()[fsid].data = ramtab_table()[fsid].own;
		return 0;
	}
	if (node != IR0_EARLY_ROOTFS_INIT)
		return -EINVAL;
	g_init_vis = length < g_blob_len ? length : g_blob_len;
	return 0;
}

int64_t early_rootfs_flock(int fd, int op)
{
	if (!fd_is_open(fd))
		return -EBADF;
	if (op & ~(LOCK_SH | LOCK_EX | LOCK_UN | LOCK_NB))
		return -EINVAL;
	if (!(op & (LOCK_SH | LOCK_EX | LOCK_UN)))
		return -EINVAL;
	return 0;
}

int64_t early_rootfs_utimensat(int dirfd, uint64_t path)
{
	char pbuf[32];
	enum ir0_early_rootfs_node node;

	if (!g_ready)
		return -ENOENT;
	if (dirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(dirfd) < 0)
		return -EBADF;
	if (!path)
		return fd_is_open(dirfd) ? 0 : -EBADF;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;
	node = lookup_user_path(pbuf);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -ENOENT;
	if (node == IR0_EARLY_ROOTFS_VDA && !vda_present())
		return -ENOENT;
	return 0;
}

int64_t early_rootfs_mkdirat(int dirfd, uint64_t path)
{
	char pbuf[RAMTAB_NAME];
	int ret;

	if (!g_ready)
		return -ENOENT;
	if (dirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(dirfd) < 0 &&
	    !(dirfd >= 0 && dirfd <= 2))
		return -EBADF;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;
	if (!join_under_cwd(pbuf, sizeof(pbuf)))
		return -ENAMETOOLONG;
	apply_chroot(pbuf, sizeof(pbuf));
	if (ir0_early_rootfs_path_node(pbuf) != IR0_EARLY_ROOTFS_NONE ||
	    ramtab_find(pbuf) >= 0)
		return -EEXIST;
	if (!parent_exists(pbuf) &&
	    !(product_path_live() && ir0_product_path_is_tmp(pbuf)))
		return -ENOENT;
	{
		int denied = landlock_allow(pbuf, IR0_EARLY_LANDLOCK_FS_MAKE_DIR);

		if (denied < 0)
			return denied;
	}
	if (product_path_live() && ir0_product_path_is_tmp(pbuf) &&
	    g_product_path->mkdir)
	{
		ret = g_product_path->mkdir(pbuf, 0755);
		if (ret == 0)
			g_product_path_used = 1;
		return ret;
	}
	ret = ramtab_add(pbuf, S_IFDIR | 0755U, 0, 0, 0);
	return ret < 0 ? ret : 0;
}

int64_t early_rootfs_mknodat(int dirfd, uint64_t path, uint64_t mode)
{
	char pbuf[RAMTAB_NAME];
	uint32_t type = (uint32_t)mode & S_IFMT;
	int ret;

	if (!g_ready)
		return -ENOENT;
	if (dirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(dirfd) < 0 &&
	    !(dirfd >= 0 && dirfd <= 2))
		return -EBADF;
	if (type != S_IFREG && type != S_IFIFO && type != S_IFDIR &&
	    type != S_IFSOCK)
		return -EINVAL;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;
	if (!join_under_cwd(pbuf, sizeof(pbuf)))
		return -ENAMETOOLONG;
	apply_chroot(pbuf, sizeof(pbuf));
	if (ir0_early_rootfs_path_node(pbuf) != IR0_EARLY_ROOTFS_NONE ||
	    ramtab_find(pbuf) >= 0)
		return -EEXIST;
	if (!parent_exists(pbuf))
		return -ENOENT;
	{
		uint64_t need = IR0_EARLY_LANDLOCK_FS_MAKE_REG;
		int denied;

		if (type == S_IFDIR)
			need = IR0_EARLY_LANDLOCK_FS_MAKE_DIR;
		else if (type == S_IFIFO)
			need = IR0_EARLY_LANDLOCK_FS_MAKE_FIFO;
		else if (type == S_IFSOCK)
			need = IR0_EARLY_LANDLOCK_FS_MAKE_SOCK;
		denied = landlock_allow(pbuf, need);
		if (denied < 0)
			return denied;
	}
	ret = ramtab_add(pbuf, (uint32_t)mode, 0, 0,
		     ((uint32_t)mode & S_IFMT) == S_IFREG);
	return ret < 0 ? ret : 0;
}

int64_t early_rootfs_unlinkat(int dirfd, uint64_t path, int flags)
{
	char pbuf[RAMTAB_NAME];
	enum ir0_early_rootfs_node node;
	int fsid;

	if (!g_ready)
		return -ENOENT;
	if (dirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(dirfd) < 0)
		return -EBADF;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;
	if (!join_under_cwd(pbuf, sizeof(pbuf)))
		return -ENAMETOOLONG;
	apply_chroot(pbuf, sizeof(pbuf));
	node = lookup_user_path(pbuf);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -ENOENT;
	{
		int denied = landlock_allow(pbuf,
					    node_is_dir(node) ?
					    IR0_EARLY_LANDLOCK_FS_REMOVE_DIR :
					    IR0_EARLY_LANDLOCK_FS_REMOVE_FILE);

		if (denied < 0)
			return denied;
	}
	fsid = ramtab_find(pbuf);
	if (node_is_dir(node))
	{
		if (!(flags & AT_REMOVEDIR))
			return -EISDIR;
		if (fsid < 0)
			return -EBUSY;
		if (ramtab_child_count(pbuf) > 0)
			return -ENOTEMPTY;
		ramtab_table()[fsid].used = 0;
		return 0;
	}
	if (flags & AT_REMOVEDIR)
		return -ENOTDIR;
	if (fsid < 0)
		return -EPERM;
	ramtab_rehome(fsid);
	ramtab_table()[fsid].used = 0;
	ramtab_table()[fsid].fifo_pipe = -1;
	ramtab_table()[fsid].link_to = -1;
	return 0;
}

int64_t early_rootfs_renameat(int olddirfd, uint64_t oldpath, int newdirfd,
			      uint64_t newpath)
{
	return early_rootfs_renameat2(olddirfd, oldpath, newdirfd, newpath, 0);
}

static void fs_set_path(int fsid, const char *path)
{
	unsigned i;

	for (i = 0; path[i] != '\0' && i + 1 < RAMTAB_NAME; i++)
		ramtab_table()[fsid].path[i] = path[i];
	ramtab_table()[fsid].path[i] = '\0';
}

static void unix_rebind_path(int fsid, const char *path)
{
	unsigned i;
	unsigned n;

	for (i = 0; i < UNIX_MAX; i++)
	{
		if (g_unix[i].used && g_unix[i].bound_fs == fsid)
		{
			for (n = 0; path[n] != '\0' &&
				    n + 1 < sizeof(g_unix[i].bound_path);
			     n++)
				g_unix[i].bound_path[n] = path[n];
			g_unix[i].bound_path[n] = '\0';
		}
	}
}

int64_t early_rootfs_renameat2(int olddirfd, uint64_t oldpath, int newdirfd,
			       uint64_t newpath, unsigned flags)
{
	char oldbuf[RAMTAB_NAME];
	char newbuf[RAMTAB_NAME];
	char saved[RAMTAB_NAME];
	enum ir0_early_rootfs_node node;
	int fsid;
	int dest;
	unsigned i;

	if (flags & IR0_EARLY_RENAME_WHITEOUT)
		return -EOPNOTSUPP;
	if (!ir0_early_renameat2_flags_ok(flags))
		return -EINVAL;
	if (!g_ready)
		return -ENOENT;
	if (olddirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(olddirfd) < 0)
		return -EBADF;
	if (newdirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(newdirfd) < 0 &&
	    newdirfd != olddirfd)
		return -EBADF;
	if (!copy_user_cstr(oldpath, oldbuf, sizeof(oldbuf)))
		return -EFAULT;
	if (!copy_user_cstr(newpath, newbuf, sizeof(newbuf)))
		return -EFAULT;
	if (!join_under_cwd(oldbuf, sizeof(oldbuf)) ||
	    !join_under_cwd(newbuf, sizeof(newbuf)))
		return -ENAMETOOLONG;
	apply_chroot(oldbuf, sizeof(oldbuf));
	apply_chroot(newbuf, sizeof(newbuf));
	node = lookup_user_path(oldbuf);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -ENOENT;
	{
		int denied = landlock_allow(oldbuf,
					    IR0_EARLY_LANDLOCK_FS_REFER);

		if (denied < 0)
			return denied;
		denied = landlock_allow(newbuf, IR0_EARLY_LANDLOCK_FS_REFER);
		if (denied < 0)
			return denied;
	}
	fsid = ramtab_find(oldbuf);
	if (fsid < 0)
		return -EACCES;
	if (ir0_early_rootfs_path_node(newbuf) != IR0_EARLY_ROOTFS_NONE)
		return -EACCES;
	if (!parent_exists(newbuf))
		return -ENOENT;
	dest = ramtab_find(newbuf);
	if (flags & IR0_EARLY_RENAME_EXCHANGE)
	{
		if (dest < 0)
			return -ENOENT;
		for (i = 0; ramtab_table()[fsid].path[i] != '\0' && i + 1 < RAMTAB_NAME; i++)
			saved[i] = ramtab_table()[fsid].path[i];
		saved[i] = '\0';
		fs_set_path(fsid, ramtab_table()[dest].path);
		fs_set_path(dest, saved);
		unix_rebind_path(fsid, ramtab_table()[fsid].path);
		unix_rebind_path(dest, ramtab_table()[dest].path);
		return 0;
	}
	if (dest >= 0 && (flags & IR0_EARLY_RENAME_NOREPLACE))
		return -EEXIST;
	if (dest >= 0)
	{
		if (ramtab_node_kind(ramtab_table()[dest].mode) == IR0_EARLY_ROOTFS_USERDIR)
			return -EISDIR;
		ramtab_table()[dest].used = 0;
	}
	for (i = 0; newbuf[i] != '\0' && i + 1 < RAMTAB_NAME; i++)
		;
	if (newbuf[i] != '\0')
		return -ENAMETOOLONG;
	fs_set_path(fsid, newbuf);
	if (node == IR0_EARLY_ROOTFS_UNIX)
		unix_rebind_path(fsid, newbuf);
	return 0;
}
