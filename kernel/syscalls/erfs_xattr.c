/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel/syscalls/erfs_xattr.c
 * Description: staged rootfs extended attributes, file handles, xattrat,
 *              cachestat and file_get/setattr.
 */

#include "erfs_internal.h"

#include <ir0/early_process.h>

static int xa_name_eq(const char *left, const char *right)
{
	if (!left || !right)
		return 0;
	while (*left && *left == *right)
	{
		left++;
		right++;
	}
	return *left == *right;
}

static int xa_find(enum ir0_early_rootfs_node node, int id, const char *name)
{
	unsigned i;

	for (i = 0; i < XA_MAX; i++)
	{
		if (g_xattr[i].used && g_xattr[i].node == node &&
		    g_xattr[i].id == id && xa_name_eq(g_xattr[i].name, name))
			return (int)i;
	}
	return -1;
}

static int resolve_xattr_target(int dirfd, uint64_t path, int fd, int nofollow,
				enum ir0_early_rootfs_node *node, int *id)
{
	char pbuf[32];

	if (fd >= 0)
	{
		*node = node_of_fd(fd);
		if (*node == IR0_EARLY_ROOTFS_NONE)
			return -EBADF;
		*id = pipe_id_of_fd(fd);
		return 0;
	}
	if (!g_ready)
		return -ENOENT;
	if (dirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(dirfd) < 0)
		return -EBADF;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;
	if (!join_under_cwd(pbuf, sizeof(pbuf)))
		return -ENAMETOOLONG;
	*node = lookup_user_path(pbuf);
	if (*node == IR0_EARLY_ROOTFS_SYMLINK && !nofollow)
	{
		int walked = follow_path(pbuf, sizeof(pbuf));

		if (walked < 0)
			return -ELOOP;
		if (!walked)
			return -ENOENT;
		*node = lookup_user_path(pbuf);
	}
	if (*node == IR0_EARLY_ROOTFS_NONE)
		return -ENOENT;
	*id = ramtab_find(pbuf);
	return 0;
}

int64_t early_rootfs_xattr(int dirfd, uint64_t path, int fd, uint64_t name,
			   uint64_t value, uint64_t size, unsigned flags,
			   int op, int nofollow)
{
	enum ir0_early_rootfs_node node;
	int id;
	int slot;
	int ret;
	char nbuf[IR0_EARLY_XATTR_NAME_MAX + 1];
	unsigned i;

	ret = resolve_xattr_target(dirfd, path, fd, nofollow, &node, &id);
	if (ret < 0)
		return ret;
	if (op != 2)
	{
		if (!name || !copy_user_cstr(name, nbuf, sizeof(nbuf)))
			return -EFAULT;
		if (nbuf[0] == 0)
			return -EINVAL;
	}
	if (op == 0)
	{
		uint8_t raw[IR0_EARLY_XATTR_VALUE_MAX];

		if (flags & ~(IR0_EARLY_XATTR_CREATE | IR0_EARLY_XATTR_REPLACE))
			return -EINVAL;
		if ((flags & IR0_EARLY_XATTR_CREATE) &&
		    (flags & IR0_EARLY_XATTR_REPLACE))
			return -EINVAL;
		if (size > IR0_EARLY_XATTR_VALUE_MAX)
			return -E2BIG;
		if (size && (!g_user || !g_user->copy_from_user ||
			     g_user->copy_from_user(raw, value, size) != 0))
			return -EFAULT;
		slot = xa_find(node, id, nbuf);
		if (slot >= 0 && (flags & IR0_EARLY_XATTR_CREATE))
			return -EEXIST;
		if (slot < 0 && (flags & IR0_EARLY_XATTR_REPLACE))
			return -ENODATA;
		if (slot < 0)
		{
			for (slot = 0; slot < XA_MAX; slot++)
			{
				if (!g_xattr[slot].used)
					break;
			}
			if (slot >= XA_MAX)
				return -ENOSPC;
			g_xattr[slot].used = 1;
			g_xattr[slot].node = node;
			g_xattr[slot].id = id;
			for (i = 0; i < sizeof(nbuf); i++)
				g_xattr[slot].name[i] = nbuf[i];
		}
		g_xattr[slot].vlen = (uint32_t)size;
		for (i = 0; i < size; i++)
			g_xattr[slot].value[i] = raw[i];
		return 0;
	}
	if (op == 1)
	{
		slot = xa_find(node, id, nbuf);
		if (slot < 0)
			return -ENODATA;
		if (!size)
			return (int64_t)g_xattr[slot].vlen;
		if (size < g_xattr[slot].vlen)
			return -ERANGE;
		if (!g_user || !g_user->copy_to_user ||
		    g_user->copy_to_user(value, g_xattr[slot].value,
					 g_xattr[slot].vlen) != 0)
			return -EFAULT;
		return (int64_t)g_xattr[slot].vlen;
	}
	if (op == 2)
	{
		uint32_t need = 0;
		uint32_t copied = 0;

		for (i = 0; i < XA_MAX; i++)
		{
			unsigned nlen = 0;

			if (!g_xattr[i].used || g_xattr[i].node != node ||
			    g_xattr[i].id != id)
				continue;
			while (g_xattr[i].name[nlen])
				nlen++;
			need += nlen + 1U;
		}
		if (!size)
			return (int64_t)need;
		if (size < need)
			return -ERANGE;
		for (i = 0; i < XA_MAX; i++)
		{
			unsigned nlen = 0;

			if (!g_xattr[i].used || g_xattr[i].node != node ||
			    g_xattr[i].id != id)
				continue;
			while (g_xattr[i].name[nlen])
				nlen++;
			if (!g_user || !g_user->copy_to_user ||
			    g_user->copy_to_user(value + copied,
						 g_xattr[i].name,
						 nlen + 1U) != 0)
				return -EFAULT;
			copied += nlen + 1U;
		}
		return (int64_t)copied;
	}
	if (op == 3)
	{
		slot = xa_find(node, id, nbuf);
		if (slot < 0)
			return -ENODATA;
		g_xattr[slot].used = 0;
		return 0;
	}
	return -EINVAL;
}

int64_t early_rootfs_name_to_handle_at(int dirfd, uint64_t path,
				       uint64_t handle, uint64_t mnt_id,
				       int flags)
{
	enum ir0_early_rootfs_node node;
	int id;
	uint32_t bytes;
	int32_t type;
	uint32_t ino;
	uint32_t gen = 1;
	int32_t mount = 1;
	char pbuf[32];

	if (flags & ~(IR0_EARLY_AT_EMPTY_PATH | IR0_EARLY_AT_SYMLINK_FOLLOW))
		return -EINVAL;
	if (!handle)
		return -EFAULT;
	if (!g_user || !g_user->copy_from_user || !g_user->copy_to_user)
		return -EFAULT;
	if (g_user->copy_from_user(&bytes, handle, sizeof(bytes)) != 0)
		return -EFAULT;
	if ((flags & IR0_EARLY_AT_EMPTY_PATH) && path)
	{
		if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
			return -EFAULT;
		if (pbuf[0] == 0)
		{
			if (dirfd == IR0_EARLY_ROOTFS_AT_FDCWD)
			{
				node = g_cwd;
				id = -1;
			}
			else
			{
				node = node_of_fd(dirfd);
				if (node == IR0_EARLY_ROOTFS_NONE)
					return -EBADF;
				id = pipe_id_of_fd(dirfd);
			}
			goto encode;
		}
	}
	{
		int ret = resolve_xattr_target(dirfd, path, -1,
					       !(flags & IR0_EARLY_AT_SYMLINK_FOLLOW),
					       &node, &id);

		if (ret < 0)
			return ret;
	}
encode:
	if (bytes < IR0_EARLY_HANDLE_DATA)
	{
		bytes = IR0_EARLY_HANDLE_DATA;
		if (g_user->copy_to_user(handle, &bytes, sizeof(bytes)) != 0)
			return -EFAULT;
		return -EOVERFLOW;
	}
	bytes = IR0_EARLY_HANDLE_DATA;
	type = IR0_EARLY_FILEID_INO32_GEN;
	ino = (uint32_t)node;
	if (g_user->copy_to_user(handle, &bytes, sizeof(bytes)) != 0 ||
	    g_user->copy_to_user(handle + 4, &type, sizeof(type)) != 0 ||
	    g_user->copy_to_user(handle + 8, &ino, sizeof(ino)) != 0 ||
	    g_user->copy_to_user(handle + 12, &gen, sizeof(gen)) != 0)
		return -EFAULT;
	if (id >= 0)
	{
		uint32_t packed = (uint32_t)id | 0x80000000U;

		if (g_user->copy_to_user(handle + 12, &packed, sizeof(packed)) != 0)
			return -EFAULT;
	}
	if (mnt_id &&
	    g_user->copy_to_user(mnt_id, &mount, sizeof(mount)) != 0)
		return -EFAULT;
	return 0;
}

int64_t early_rootfs_open_by_handle_at(int mount_fd, uint64_t handle, int flags)
{
	uint32_t bytes;
	int32_t type;
	uint32_t ino;
	uint32_t gen;
	enum ir0_early_rootfs_node node;
	int id;
	int fd;
	int idx;

	if (!early_process_is_root())
		return -EPERM;
	if (mount_fd != IR0_EARLY_ROOTFS_AT_FDCWD && !fd_is_open(mount_fd))
		return -EBADF;
	if (!handle || !g_user || !g_user->copy_from_user)
		return -EFAULT;
	if (g_user->copy_from_user(&bytes, handle, sizeof(bytes)) != 0 ||
	    g_user->copy_from_user(&type, handle + 4, sizeof(type)) != 0)
		return -EFAULT;
	if (bytes < IR0_EARLY_HANDLE_DATA ||
	    type != IR0_EARLY_FILEID_INO32_GEN)
		return -EINVAL;
	if (g_user->copy_from_user(&ino, handle + 8, sizeof(ino)) != 0 ||
	    g_user->copy_from_user(&gen, handle + 12, sizeof(gen)) != 0)
		return -EFAULT;
	node = (enum ir0_early_rootfs_node)ino;
	if (node <= IR0_EARLY_ROOTFS_NONE || node > IR0_EARLY_ROOTFS_RULESET)
		return -ESTALE;
	id = (gen & 0x80000000U) ? (int)(gen & 0x7fffffffU) : -1;
	fd = alloc_fd(node);
	if (fd < 0)
		return fd;
	idx = slot_from_fd(fd);
	if (idx >= 0 && id >= 0)
		g_slots[idx].pipe_id = id;
	set_flags_of_fd(fd, status_flags(flags));
	set_cloexec_of_fd(fd, (flags & LINUX_O_CLOEXEC) != 0);
	if (node == IR0_EARLY_ROOTFS_MEMFD && id >= 0 && id < MEMFD_MAX &&
	    g_memfd[id].used)
		g_memfd[id].refs++;
	if ((node == IR0_EARLY_ROOTFS_FILE || node == IR0_EARLY_ROOTFS_FIFO ||
	     node == IR0_EARLY_ROOTFS_USERDIR) &&
	    (id < 0 || id >= RAMTAB_MAX || !ramtab_table()[id].used))
	{
		close_fd(fd);
		return -ESTALE;
	}
	return fd;
}

static int resolve_at_path(int dirfd, uint64_t path, unsigned flags,
			   enum ir0_early_rootfs_node *node, int *id)
{
	char pbuf[32];
	int nofollow = (flags & IR0_EARLY_AT_SYMLINK_NOFOLLOW) != 0;

	if (flags & IR0_EARLY_AT_EMPTY_PATH)
	{
		if (!path)
		{
			if (dirfd == IR0_EARLY_ROOTFS_AT_FDCWD)
			{
				*node = g_cwd;
				*id = -1;
				return 0;
			}
			return resolve_xattr_target(0, 0, dirfd, 0, node, id);
		}
		if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
			return -EFAULT;
		if (pbuf[0] == 0)
		{
			if (dirfd == IR0_EARLY_ROOTFS_AT_FDCWD)
			{
				*node = g_cwd;
				*id = -1;
				return 0;
			}
			return resolve_xattr_target(0, 0, dirfd, 0, node, id);
		}
	}
	return resolve_xattr_target(dirfd, path, -1, nofollow, node, id);
}

static int copy_struct_from_user(void *dest, uint64_t usize, uint64_t src,
				 uint64_t ksize)
{
	uint8_t extra;
	uint64_t i;

	if (usize < ksize)
		return -EINVAL;
	if (usize > 4096)
		return -E2BIG;
	if (!g_user || !g_user->copy_from_user)
		return -EFAULT;
	if (g_user->copy_from_user(dest, src, ksize) != 0)
		return -EFAULT;
	for (i = ksize; i < usize; i++)
	{
		if (g_user->copy_from_user(&extra, src + i, 1) != 0)
			return -EFAULT;
		if (extra)
			return -E2BIG;
	}
	return 0;
}

int64_t early_rootfs_xattrat(int dirfd, uint64_t path, unsigned at_flags,
			     uint64_t name, uint64_t args, uint64_t usize,
			     int op)
{
	enum ir0_early_rootfs_node node;
	int id;
	int fd = -1;
	int ret;
	uint64_t value = 0;
	uint64_t size = 0;
	unsigned xaflags = 0;

	if (at_flags & ~IR0_EARLY_XATTRAT_AT_FLAGS)
		return -EINVAL;
	if (op == 0 || op == 1)
	{
		uint64_t packed[2];

		if (!args)
			return -EFAULT;
		ret = copy_struct_from_user(packed, usize, args,
					    IR0_EARLY_XATTR_ARGS_BYTES);
		if (ret < 0)
			return ret;
		value = packed[0];
		size = (uint64_t)(uint32_t)packed[1];
		xaflags = (unsigned)(uint32_t)(packed[1] >> 32);
	}
	else if (op == 2)
	{
		value = args;
		size = usize;
	}
	ret = resolve_at_path(dirfd, path, at_flags, &node, &id);
	if (ret < 0)
		return ret;
	if ((at_flags & IR0_EARLY_AT_EMPTY_PATH) &&
	    dirfd != IR0_EARLY_ROOTFS_AT_FDCWD)
	{
		char pbuf[32];

		if (!path ||
		    (copy_user_cstr(path, pbuf, sizeof(pbuf)) && pbuf[0] == 0))
			fd = dirfd;
	}
	if (fd >= 0)
		return early_rootfs_xattr(0, 0, fd, name, value, size, xaflags,
					  op, 0);
	return early_rootfs_xattr(dirfd, path, -1, name, value, size, xaflags,
				  op, (at_flags & IR0_EARLY_AT_SYMLINK_NOFOLLOW) != 0);
}

int64_t early_rootfs_cachestat(int fd, uint64_t range, uint64_t out,
			       unsigned flags)
{
	enum ir0_early_rootfs_node node;
	uint64_t csr[2];
	uint64_t cs[5];
	uint64_t off;
	uint64_t len;
	uint64_t filesz = 0;
	uint64_t covered;
	unsigned i;

	node = node_of_fd(fd);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -EBADF;
	if (flags)
		return -EINVAL;
	if (!range || !out || !g_user || !g_user->copy_from_user ||
	    !g_user->copy_to_user)
		return -EFAULT;
	if (g_user->copy_from_user(csr, range, sizeof(csr)) != 0)
		return -EFAULT;
	off = csr[0];
	len = csr[1];
	if (node == IR0_EARLY_ROOTFS_INIT)
		filesz = g_init_vis;
	else if (node == IR0_EARLY_ROOTFS_MEMFD)
	{
		int mid = pipe_id_of_fd(fd);

		if (mid < 0 || mid >= MEMFD_MAX || !g_memfd[mid].used)
			return -EBADF;
		filesz = g_memfd[mid].len;
	}
	else if (node == IR0_EARLY_ROOTFS_FILE)
	{
		int fsid = pipe_id_of_fd(fd);

		if (fsid < 0 || fsid >= RAMTAB_MAX || !ramtab_table()[fsid].used)
			return -EBADF;
		filesz = ramtab_table()[fsid].size;
	}
	for (i = 0; i < 5; i++)
		cs[i] = 0;
	if (off < filesz)
	{
		covered = filesz - off;
		if (len && len < covered)
			covered = len;
		cs[0] = (covered + 4095ULL) / 4096ULL;
	}
	if (g_user->copy_to_user(out, cs, sizeof(cs)) != 0)
		return -EFAULT;
	return 0;
}

int64_t early_rootfs_file_getattr(int dirfd, uint64_t path, uint64_t attr,
				  uint64_t usize, unsigned flags)
{
	enum ir0_early_rootfs_node node;
	int id;
	int ret;
	uint32_t words[6];
	unsigned i;

	if (flags & ~(IR0_EARLY_AT_SYMLINK_NOFOLLOW | IR0_EARLY_AT_EMPTY_PATH))
		return -EINVAL;
	if (usize > 4096)
		return -E2BIG;
	if (usize < IR0_EARLY_FILE_ATTR_BYTES)
		return -EINVAL;
	if (!attr || !g_user || !g_user->copy_to_user)
		return -EFAULT;
	ret = resolve_at_path(dirfd, path, flags, &node, &id);
	if (ret < 0)
		return ret;
	for (i = 0; i < 6; i++)
		words[i] = 0;
	if (node == IR0_EARLY_ROOTFS_INIT || node == IR0_EARLY_ROOTFS_FILE ||
	    node == IR0_EARLY_ROOTFS_MEMFD)
		words[3] = 1;
	if (g_user->copy_to_user(attr, words, IR0_EARLY_FILE_ATTR_BYTES) != 0)
		return -EFAULT;
	if (usize > IR0_EARLY_FILE_ATTR_BYTES)
	{
		uint8_t zero = 0;
		uint64_t i64;

		for (i64 = IR0_EARLY_FILE_ATTR_BYTES; i64 < usize; i64++)
		{
			if (g_user->copy_to_user(attr + i64, &zero, 1) != 0)
				return -EFAULT;
		}
	}
	(void)id;
	return 0;
}

int64_t early_rootfs_file_setattr(int dirfd, uint64_t path, uint64_t attr,
				  uint64_t usize, unsigned flags)
{
	enum ir0_early_rootfs_node node;
	int id;
	int ret;
	uint32_t words[6];
	unsigned i;

	if (flags & ~(IR0_EARLY_AT_SYMLINK_NOFOLLOW | IR0_EARLY_AT_EMPTY_PATH))
		return -EINVAL;
	if (usize < IR0_EARLY_FILE_ATTR_BYTES)
		return -EINVAL;
	if (!attr)
		return -EFAULT;
	ret = copy_struct_from_user(words, usize, attr,
				    IR0_EARLY_FILE_ATTR_BYTES);
	if (ret < 0)
		return ret;
	ret = resolve_at_path(dirfd, path, flags, &node, &id);
	if (ret < 0)
		return ret;
	for (i = 0; i < 6; i++)
	{
		if (words[i])
			return -EOPNOTSUPP;
	}
	(void)id;
	(void)node;
	return 0;
}
