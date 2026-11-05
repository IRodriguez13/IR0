/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel/syscalls/early_rootfs_meta.c
 * Description: early metadata, xattr, landlock, rename and mount syscalls.
 */

#include "early_rootfs_internal.h"

int64_t early_rootfs_readlinkat(int dirfd, uint64_t path, uint64_t buffer,
				uint64_t size)
{
	static const char proc_self_exe[] = "/proc/self/exe";
	static const char init_target[] = "/init";
	char pbuf[32];
	uint64_t n = sizeof(init_target) - 1U;

	if (!g_ready)
		return -ENOENT;
	if (dirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(dirfd) < 0)
		return -EBADF;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;
	if (!join_under_cwd(pbuf, sizeof(pbuf)))
		return -ENAMETOOLONG;
	apply_chroot(pbuf, sizeof(pbuf));
	if (!strings_equal(pbuf, proc_self_exe))
	{
		int fsid = ramtab_find(pbuf);
		enum ir0_early_rootfs_node node = lookup_user_path(pbuf);

		if (node == IR0_EARLY_ROOTFS_SYMLINK && fsid >= 0)
		{
			uint64_t have = ramtab_table()[fsid].size;

			if (have > size)
				have = size;
			if (have > 0 && (!g_user || !g_user->copy_to_user ||
					 g_user->copy_to_user(buffer,
							      ramtab_table()[fsid].data ?
								      ramtab_table()[fsid].data :
								      ramtab_table()[fsid].own,
							      have) != 0))
				return -EFAULT;
			return (int64_t)have;
		}
		if (node != IR0_EARLY_ROOTFS_NONE)
			return -EINVAL;
		return -ENOENT;
	}
	if (size < n)
		n = size;
	if (n > 0 && (!g_user || !g_user->copy_to_user ||
		      g_user->copy_to_user(buffer, init_target, n) != 0))
		return -EFAULT;
	return (int64_t)n;
}

int64_t early_rootfs_symlinkat(uint64_t target, int newdirfd, uint64_t linkpath)
{
	char tbuf[RAMTAB_NAME];
	char pbuf[RAMTAB_NAME];
	int ret;
	unsigned n;

	if (!g_ready)
		return -ENOENT;
	if (newdirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(newdirfd) < 0 &&
	    !(newdirfd >= 0 && newdirfd <= 2))
		return -EBADF;
	if (!copy_user_cstr(target, tbuf, sizeof(tbuf)))
		return -EFAULT;
	if (!copy_user_cstr(linkpath, pbuf, sizeof(pbuf)))
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
		int denied = landlock_allow(pbuf, IR0_EARLY_LANDLOCK_FS_MAKE_SYM);

		if (denied < 0)
			return denied;
	}
	n = 0;
	while (tbuf[n] != '\0')
		n++;
	ret = ramtab_add(pbuf, S_IFLNK | 0777U, n, (const uint8_t *)tbuf, 1);
	return ret < 0 ? ret : 0;
}

int64_t early_rootfs_linkat(int olddirfd, uint64_t oldpath, int newdirfd,
			    uint64_t newpath, int flags)
{
	char oldbuf[RAMTAB_NAME];
	char newbuf[RAMTAB_NAME];
	enum ir0_early_rootfs_node node;
	int src;
	int dest;
	int body;

	if (!g_ready)
		return -ENOENT;
	if (olddirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(olddirfd) < 0)
		return -EBADF;
	if (newdirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(newdirfd) < 0 &&
	    newdirfd != olddirfd)
		return -EBADF;
	if (flags & ~(IR0_EARLY_AT_SYMLINK_FOLLOW))
		return -EINVAL;
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
	if (node == IR0_EARLY_ROOTFS_SYMLINK &&
	    (flags & IR0_EARLY_AT_SYMLINK_FOLLOW))
	{
		if (follow_path(oldbuf, sizeof(oldbuf)) < 0)
			return -ELOOP;
		node = lookup_user_path(oldbuf);
	}
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -ENOENT;
	if (node == IR0_EARLY_ROOTFS_USERDIR || node_is_dir(node))
		return -EPERM;
	src = ramtab_find(oldbuf);
	if (src < 0)
		return -EPERM;
	if (ir0_early_rootfs_path_node(newbuf) != IR0_EARLY_ROOTFS_NONE ||
	    ramtab_find(newbuf) >= 0)
		return -EEXIST;
	if (!parent_exists(newbuf))
		return -ENOENT;
	{
		int denied = landlock_allow(newbuf,
					    IR0_EARLY_LANDLOCK_FS_MAKE_REG);

		if (denied < 0)
			return denied;
	}
	body = ramtab_body(src);
	dest = ramtab_add(newbuf, ramtab_table()[body].mode, ramtab_table()[body].size, ramtab_table()[body].data,
		      0);
	if (dest < 0)
		return dest;
	ramtab_table()[dest].link_to = body;
	ramtab_table()[dest].writable = 0;
	ramtab_table()[dest].data = 0;
	ramtab_table()[dest].size = 0;
	return 0;
}

static void fill_statfs(struct ir0_statfs *st)
{
	unsigned i;

	for (i = 0; i < sizeof(*st); i++)
		((unsigned char *)st)[i] = 0;
	st->f_type = IR0_TMPFS_MAGIC;
	st->f_bsize = 4096;
	st->f_frsize = 4096;
	st->f_namelen = 255;
	if (vda_present())
	{
		st->f_blocks = ir0_block_sector_count_by_name("vda") / 8ULL;
		st->f_bfree = st->f_blocks;
		st->f_bavail = st->f_blocks;
	}
	else
	{
		st->f_blocks = 1;
		st->f_bfree = 1;
		st->f_bavail = 1;
	}
	st->f_files = 16;
	st->f_ffree = 8;
}

int64_t early_rootfs_statfs(uint64_t path, uint64_t buf)
{
	char pbuf[32];
	enum ir0_early_rootfs_node node;
	struct ir0_statfs st;

	if (!g_ready)
		return -ENOENT;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;
	node = lookup_user_path(pbuf);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -ENOENT;
	if (node == IR0_EARLY_ROOTFS_VDA && !vda_present())
		return -ENOENT;
	fill_statfs(&st);
	if (!g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(buf, &st, sizeof(st)) != 0)
		return -EFAULT;
	return 0;
}

int64_t early_rootfs_fstatfs(int fd, uint64_t buf)
{
	struct ir0_statfs st;

	if (!g_ready)
		return -EBADF;
	if (!fd_is_open(fd))
		return -EBADF;
	fill_statfs(&st);
	if (!g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(buf, &st, sizeof(st)) != 0)
		return -EFAULT;
	return 0;
}

static void apply_stored_mode(enum ir0_early_rootfs_node node, int id,
			      uint64_t mode)
{
	uint32_t perm = (uint32_t)mode & 07777U;

	if (node == IR0_EARLY_ROOTFS_FILE || node == IR0_EARLY_ROOTFS_FIFO ||
	    node == IR0_EARLY_ROOTFS_USERDIR ||
	    node == IR0_EARLY_ROOTFS_SYMLINK)
	{
		int fsid = ramtab_body(id);

		if (fsid >= 0 && fsid < RAMTAB_MAX && ramtab_table()[fsid].used)
			ramtab_table()[fsid].mode = (ramtab_table()[fsid].mode & S_IFMT) | perm;
	}
	if (node == IR0_EARLY_ROOTFS_MEMFD && id >= 0 && id < MEMFD_MAX &&
	    g_memfd[id].used)
		g_memfd[id].mode = (g_memfd[id].mode & S_IFMT) | perm;
}

int64_t early_rootfs_fchmod(int fd, uint64_t mode)
{
	if (!g_ready || !fd_is_open(fd))
		return -EBADF;
	apply_stored_mode(node_of_fd(fd), pipe_id_of_fd(fd), mode);
	return 0;
}

int64_t early_rootfs_fchmodat(int dirfd, uint64_t path, uint64_t mode)
{
	return early_rootfs_fchmodat2(dirfd, path, mode, 0);
}

int64_t early_rootfs_fchmodat2(int dirfd, uint64_t path, uint64_t mode,
			       unsigned flags)
{
	char pbuf[32];
	enum ir0_early_rootfs_node node;
	int fsid;

	if (flags & ~IR0_EARLY_FCHMODAT2_FLAGS)
		return -EINVAL;
	if (!g_ready)
		return -ENOENT;
	if (dirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(dirfd) < 0 &&
	    !fd_is_open(dirfd))
		return -EBADF;
	if ((flags & IR0_EARLY_AT_EMPTY_PATH) && path)
	{
		if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
			return -EFAULT;
		if (pbuf[0] == 0)
		{
			if (dirfd == IR0_EARLY_ROOTFS_AT_FDCWD)
			{
				apply_stored_mode(g_cwd, -1, mode);
				return 0;
			}
			return early_rootfs_fchmod(dirfd, mode);
		}
	}
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
	fsid = ramtab_find(pbuf);
	apply_stored_mode(node, fsid, mode);
	return 0;
}

int64_t early_rootfs_fchown(int fd, uint64_t uid, uint64_t gid)
{
	(void)uid;
	(void)gid;
	return early_rootfs_fchmod(fd, 0);
}

int64_t early_rootfs_fchownat(int dirfd, uint64_t path, uint64_t uid,
			      uint64_t gid)
{
	(void)uid;
	(void)gid;
	return early_rootfs_fchmodat(dirfd, path, 0);
}

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

void copy_kstr(char *dst, unsigned cap, const char *src)
{
	unsigned i;

	if (!dst || !cap)
		return;
	if (!src)
	{
		dst[0] = '\0';
		return;
	}
	for (i = 0; src[i] != '\0' && i + 1 < cap; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

static int path_is_beneath(const char *path, const char *parent)
{
	unsigned i = 0;

	if (!path || !parent || parent[0] == '\0' || path[0] != '/')
		return 0;
	if (parent[0] == '/' && parent[1] == '\0')
		return 1;
	while (parent[i] && path[i] == parent[i])
		i++;
	if (parent[i] != '\0')
		return 0;
	return path[i] == '\0' || path[i] == '/';
}

int landlock_allow(const char *path, uint64_t need)
{
	uint64_t want;
	uint64_t got = 0;
	int i;

	if (!g_ll_on || !need || !path)
		return 0;
	want = need & g_ll_handled;
	if (!want)
		return 0;
	for (i = 0; i < g_ll_n; i++)
	{
		if (path_is_beneath(path, g_ll_rules[i].path))
			got |= g_ll_rules[i].allowed;
	}
	if ((got & want) == want)
		return 0;
	return -EACCES;
}

int landlock_open_need(const char *path, int flags)
{
	char look[RAMTAB_NAME];
	enum ir0_early_rootfs_node node;
	uint64_t need = 0;
	int acc = flags & LINUX_O_ACCMODE;

	copy_kstr(look, sizeof(look), path);
	node = lookup_user_path(look);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return 0;
	if (node_is_dir(node) || (flags & LINUX_O_DIRECTORY))
		need |= IR0_EARLY_LANDLOCK_FS_READ_DIR;
	else
	{
		if (acc != 1)
			need |= IR0_EARLY_LANDLOCK_FS_READ_FILE;
		if (acc != 0)
			need |= IR0_EARLY_LANDLOCK_FS_WRITE_FILE;
	}
	if (flags & LINUX_O_TRUNC)
		need |= IR0_EARLY_LANDLOCK_FS_TRUNCATE;
	return landlock_allow(path, need);
}

int64_t early_rootfs_landlock_create(uint64_t attr, uint64_t size,
				     unsigned flags)
{
	uint64_t handled = 0;
	int slot;
	int fd;
	int idx;

	if (flags)
	{
		if (attr || size)
			return -EINVAL;
		if (flags == IR0_EARLY_LANDLOCK_CREATE_VERSION)
			return IR0_EARLY_LANDLOCK_ABI;
		if (flags == IR0_EARLY_LANDLOCK_CREATE_ERRATA)
			return 0;
		return -EINVAL;
	}
	if (size < 8)
		return -EINVAL;
	if (size > 4096)
		return -E2BIG;
	if (!attr || !g_user || !g_user->copy_from_user)
		return -EFAULT;
	if (g_user->copy_from_user(&handled, attr, 8) != 0)
		return -EFAULT;
	if (size > 8)
	{
		uint64_t extra;
		uint64_t off;

		for (off = 8; off + 8 <= size && off < 48; off += 8)
		{
			if (g_user->copy_from_user(&extra, attr + off, 8) != 0)
				return -EFAULT;
			if (extra)
				return -EINVAL;
		}
		for (; off < size; off++)
		{
			uint8_t byte;

			if (g_user->copy_from_user(&byte, attr + off, 1) != 0)
				return -EFAULT;
			if (byte)
				return -E2BIG;
		}
	}
	if ((handled | IR0_EARLY_LANDLOCK_FS_MASK) != IR0_EARLY_LANDLOCK_FS_MASK)
		return -EINVAL;
	if (!handled)
		return -ENOMSG;
	for (slot = 0; slot < RULESET_MAX; slot++)
	{
		if (!g_ruleset[slot].used)
			break;
	}
	if (slot >= RULESET_MAX)
		return -EMFILE;
	g_ruleset[slot].used = 1;
	g_ruleset[slot].refs = 1;
	g_ruleset[slot].handled = handled;
	g_ruleset[slot].nrules = 0;
	fd = alloc_fd(IR0_EARLY_ROOTFS_RULESET);
	if (fd < 0)
	{
		g_ruleset[slot].used = 0;
		return fd;
	}
	idx = slot_from_fd(fd);
	if (idx >= 0)
		g_slots[idx].pipe_id = slot;
	set_cloexec_of_fd(fd, 1);
	return fd;
}

int64_t early_rootfs_landlock_add(int fd, int type, uint64_t rule,
				  unsigned flags)
{
	int id;
	uint64_t allowed = 0;
	int32_t parent = 0;
	char pbuf[RAMTAB_NAME];
	int ret;

	if (flags & ~IR0_EARLY_LANDLOCK_ADD_QUIET)
		return -EINVAL;
	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_RULESET)
		return node_of_fd(fd) == IR0_EARLY_ROOTFS_NONE ? -EBADF : -EBADFD;
	id = pipe_id_of_fd(fd);
	if (id < 0 || id >= RULESET_MAX || !g_ruleset[id].used)
		return -EBADFD;
	if (type == IR0_EARLY_LANDLOCK_RULE_NET_PORT)
		return -EAFNOSUPPORT;
	if (type != IR0_EARLY_LANDLOCK_RULE_PATH_BENEATH)
		return -EINVAL;
	if (!rule || !g_user || !g_user->copy_from_user)
		return -EFAULT;
	if (g_user->copy_from_user(&allowed, rule, 8) != 0 ||
	    g_user->copy_from_user(&parent, rule + 8, 4) != 0)
		return -EFAULT;
	if (!flags && !allowed)
		return -ENOMSG;
	if ((allowed | g_ruleset[id].handled) != g_ruleset[id].handled)
		return -EINVAL;
	if ((flags & IR0_EARLY_LANDLOCK_ADD_QUIET) && !g_ruleset[id].handled)
		return -EINVAL;
	ret = path_of_fd(parent, pbuf, sizeof(pbuf));
	if (ret < 0)
		return ret;
	if (g_ruleset[id].nrules >= LL_RULE_MAX)
		return -E2BIG;
	copy_kstr(g_ruleset[id].rules[g_ruleset[id].nrules].path,
		  sizeof(g_ruleset[id].rules[0].path), pbuf);
	g_ruleset[id].rules[g_ruleset[id].nrules].allowed = allowed;
	g_ruleset[id].nrules++;
	return 0;
}

int64_t early_rootfs_landlock_restrict(int fd, unsigned flags)
{
	int id;
	int i;

	if ((flags | IR0_EARLY_LANDLOCK_RESTRICT_MASK) !=
	    IR0_EARLY_LANDLOCK_RESTRICT_MASK)
		return -EINVAL;
	if (!early_process_is_root())
		return -EPERM;
	if (g_ll_on)
		return -E2BIG;
	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_RULESET)
		return node_of_fd(fd) == IR0_EARLY_ROOTFS_NONE ? -EBADF : -EBADFD;
	id = pipe_id_of_fd(fd);
	if (id < 0 || id >= RULESET_MAX || !g_ruleset[id].used)
		return -EBADFD;
	g_ll_handled = g_ruleset[id].handled;
	g_ll_n = g_ruleset[id].nrules;
	for (i = 0; i < g_ll_n; i++)
	{
		g_ll_rules[i].allowed = g_ruleset[id].rules[i].allowed;
		copy_kstr(g_ll_rules[i].path, sizeof(g_ll_rules[i].path),
			  g_ruleset[id].rules[i].path);
	}
	g_ll_on = 1;
	return 0;
}

static int copy_mnt_req(uint64_t req, uint64_t *mnt_id, uint32_t *mnt_fd,
			uint64_t *param, unsigned flags)
{
	uint32_t size = 0;
	uint32_t spare = 0;
	uint64_t id = 0;
	uint64_t prm = 0;

	if (!req || !g_user || !g_user->copy_from_user)
		return -EFAULT;
	if (g_user->copy_from_user(&size, req, 4) != 0)
		return -EFAULT;
	if (size > 4096)
		return -E2BIG;
	if (size < IR0_EARLY_MNT_ID_REQ_VER0)
		return -EINVAL;
	if (g_user->copy_from_user(&spare, req + 4, 4) != 0 ||
	    g_user->copy_from_user(&id, req + 8, 8) != 0 ||
	    g_user->copy_from_user(&prm, req + 16, 8) != 0)
		return -EFAULT;
	if (size > IR0_EARLY_MNT_ID_REQ_VER0)
	{
		uint64_t off;
		uint8_t byte;

		for (off = IR0_EARLY_MNT_ID_REQ_VER0; off < size; off++)
		{
			if (g_user->copy_from_user(&byte, req + off, 1) != 0)
				return -EFAULT;
			if (byte)
				return -E2BIG;
		}
	}
	if (flags & IR0_EARLY_STATMOUNT_BY_FD)
	{
		if (id)
			return -EINVAL;
	}
	else if (id != IR0_EARLY_LSMT_ROOT && id <= 0x80000000ULL)
		return -EINVAL;
	*mnt_id = id;
	*mnt_fd = spare;
	*param = prm;
	return 0;
}

int64_t early_rootfs_statmount(uint64_t req, uint64_t buf, uint64_t bufsize,
			       unsigned flags)
{
	uint64_t mnt_id = 0;
	uint32_t mnt_fd = 0;
	uint64_t mask = 0;
	uint8_t out[IR0_EARLY_STATMOUNT_FIXED + 16];
	uint32_t size;
	uint64_t written;
	unsigned i;
	int ret;

	if (flags & ~IR0_EARLY_STATMOUNT_BY_FD)
		return -EINVAL;
	ret = copy_mnt_req(req, &mnt_id, &mnt_fd, &mask, flags);
	if (ret < 0)
		return ret;
	if (flags & IR0_EARLY_STATMOUNT_BY_FD)
	{
		if (node_of_fd((int)mnt_fd) == IR0_EARLY_ROOTFS_NONE)
			return -EBADF;
	}
	else if (mnt_id != IR0_EARLY_MNT_ID && mnt_id != IR0_EARLY_LSMT_ROOT)
		return -ENOENT;
	if (!buf)
		return -EFAULT;
	if (bufsize < IR0_EARLY_STATMOUNT_FIXED + 10)
		return -EOVERFLOW;
	for (i = 0; i < sizeof(out); i++)
		out[i] = 0;
	written = IR0_EARLY_STATMOUNT_SB_BASIC | IR0_EARLY_STATMOUNT_MNT_BASIC |
		  IR0_EARLY_STATMOUNT_MNT_ROOT | IR0_EARLY_STATMOUNT_MNT_POINT |
		  IR0_EARLY_STATMOUNT_FS_TYPE;
	if (mask)
		written &= mask;
	size = (uint32_t)(IR0_EARLY_STATMOUNT_FIXED + 10);
	out[0] = (uint8_t)size;
	out[1] = (uint8_t)(size >> 8);
	{
		uint64_t magic = IR0_TMPFS_MAGIC;
		uint64_t mid = IR0_EARLY_MNT_ID;

		out[8] = (uint8_t)written;
		out[9] = (uint8_t)(written >> 8);
		out[20] = 1;
		for (i = 0; i < 8; i++)
			out[24 + i] = (uint8_t)(magic >> (8 * i));
		for (i = 0; i < 8; i++)
			out[40 + i] = (uint8_t)(mid >> (8 * i));
		for (i = 0; i < 8; i++)
			out[48 + i] = (uint8_t)(mid >> (8 * i));
		out[36] = (uint8_t)IR0_EARLY_STATMOUNT_FIXED;
		out[37] = (uint8_t)(IR0_EARLY_STATMOUNT_FIXED >> 8);
		out[104] = (uint8_t)(IR0_EARLY_STATMOUNT_FIXED + 6);
		out[108] = (uint8_t)(IR0_EARLY_STATMOUNT_FIXED + 8);
	}
	out[IR0_EARLY_STATMOUNT_FIXED + 0] = 't';
	out[IR0_EARLY_STATMOUNT_FIXED + 1] = 'm';
	out[IR0_EARLY_STATMOUNT_FIXED + 2] = 'p';
	out[IR0_EARLY_STATMOUNT_FIXED + 3] = 'f';
	out[IR0_EARLY_STATMOUNT_FIXED + 4] = 's';
	out[IR0_EARLY_STATMOUNT_FIXED + 5] = 0;
	out[IR0_EARLY_STATMOUNT_FIXED + 6] = '/';
	out[IR0_EARLY_STATMOUNT_FIXED + 7] = 0;
	out[IR0_EARLY_STATMOUNT_FIXED + 8] = '/';
	out[IR0_EARLY_STATMOUNT_FIXED + 9] = 0;
	if (!g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(buf, out, size) != 0)
		return -EFAULT;
	return 0;
}

int64_t early_rootfs_listmount(uint64_t req, uint64_t ids, uint64_t nr,
			       unsigned flags)
{
	uint64_t mnt_id = 0;
	uint32_t spare = 0;
	uint64_t last = 0;
	uint64_t one = IR0_EARLY_MNT_ID;
	int ret;

	if (flags & ~IR0_EARLY_LISTMOUNT_REVERSE)
		return -EINVAL;
	if (nr > 1000000)
		return -EOVERFLOW;
	ret = copy_mnt_req(req, &mnt_id, &spare, &last, 0);
	if (ret < 0)
		return ret;
	if (mnt_id != IR0_EARLY_LSMT_ROOT && mnt_id != IR0_EARLY_MNT_ID)
		return -ENOENT;
	if (last != 0 && last != IR0_EARLY_MNT_ID)
		return 0;
	if (last == IR0_EARLY_MNT_ID)
		return 0;
	if (!nr)
		return 0;
	if (!ids || !g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(ids, &one, 8) != 0)
		return -EFAULT;
	return 1;
}

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

int64_t early_rootfs_send_rights(int fd, const int *fds, int n)
{
	int pair = sock_pair_of_fd(fd);
	int uid = unix_id_of_fd(fd);
	int dest;
	int i;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (!fds || n < 0 || n > IR0_EARLY_RIGHTS_MAX)
		return -EINVAL;
	for (i = 0; i < n; i++)
	{
		if (!fd_is_open(fds[i]))
			return -EBADF;
	}
	if (pair >= 0 && pair < SOCKPAIR_MAX && g_sock[pair].used)
	{
		dest = 1 - (pipe_id_of_fd(fd) & 1);
		if (g_sock[pair].rights_n[dest])
			return -EAGAIN;
		for (i = 0; i < n; i++)
			g_sock[pair].rights[dest][i] = fds[i];
		g_sock[pair].rights_n[dest] = n;
		return 0;
	}
	if (uid >= 0 && uid < UNIX_MAX && g_unix[uid].used)
	{
		int peer = unix_find_bound(g_unix[uid].bound_path, 0);

		if (g_unix[uid].peer_fs >= 0)
			peer = unix_find_bound(ramtab_table()[g_unix[uid].peer_fs].path, 0);
		if (peer < 0)
			peer = uid;
		if (g_unix[peer].rights_n)
			return -EAGAIN;
		for (i = 0; i < n; i++)
			g_unix[peer].rights[i] = fds[i];
		g_unix[peer].rights_n = n;
		return 0;
	}
	return -ENOTCONN;
}

int64_t early_rootfs_recv_rights(int fd, int *fds, int max)
{
	int pair = sock_pair_of_fd(fd);
	int uid = unix_id_of_fd(fd);
	int src;
	int n;
	int i;
	int *stored;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (!fds || max <= 0)
		return 0;
	n = 0;
	stored = 0;
	if (pair >= 0 && pair < SOCKPAIR_MAX && g_sock[pair].used)
	{
		src = pipe_id_of_fd(fd) & 1;
		n = g_sock[pair].rights_n[src];
		stored = g_sock[pair].rights[src];
		g_sock[pair].rights_n[src] = 0;
	}
	else if (uid >= 0 && uid < UNIX_MAX && g_unix[uid].used)
	{
		n = g_unix[uid].rights_n;
		stored = g_unix[uid].rights;
		g_unix[uid].rights_n = 0;
	}
	if (n <= 0 || !stored)
		return 0;
	if (n > max)
		n = max;
	for (i = 0; i < n; i++)
	{
		int64_t newfd = early_rootfs_dup(stored[i]);

		if (newfd < 0)
			return i ? i : newfd;
		fds[i] = (int)newfd;
	}
	return n;
}

int64_t early_rootfs_close_range(unsigned first, unsigned last, unsigned flags)
{
	unsigned fd;

	if (first > last)
		return -EINVAL;
	if (!ir0_early_close_range_flags_ok(flags))
		return -EINVAL;
	if (flags & IR0_EARLY_CLOSE_RANGE_UNSHARE)
		return -EOPNOTSUPP;
	if (last > (unsigned)(ROOTFS_FD_LIMIT - 1))
		last = (unsigned)(ROOTFS_FD_LIMIT - 1);
	if (first > last)
		return 0;
	for (fd = first; fd <= last; fd++)
	{
		if (node_of_fd((int)fd) == IR0_EARLY_ROOTFS_NONE)
			continue;
		if (flags & IR0_EARLY_CLOSE_RANGE_CLOEXEC)
			set_cloexec_of_fd((int)fd, 1);
		else
			(void)early_rootfs_close((int)fd);
	}
	return 0;
}

int64_t early_rootfs_pidfd_open(int32_t pid, unsigned flags)
{
	int fd;
	int idx;

	if (flags & ~IR0_EARLY_PIDFD_NONBLOCK)
		return -EINVAL;
	if (!g_ready)
		return -ENOSYS;
	fd = alloc_fd(IR0_EARLY_ROOTFS_PIDFD);
	if (fd < 0)
		return fd;
	idx = slot_from_fd(fd);
	if (idx < 0)
	{
		close_fd(fd);
		return -EBADF;
	}
	g_slots[idx].pipe_id = pid;
	set_flags_of_fd(fd, IR0_O_RDWR |
			    ((flags & IR0_EARLY_PIDFD_NONBLOCK) ?
				     LINUX_O_NONBLOCK :
				     0));
	set_cloexec_of_fd(fd, 1);
	return fd;
}

int32_t early_rootfs_pidfd_pid(int fd)
{
	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_PIDFD)
		return -EBADF;
	return (int32_t)pipe_id_of_fd(fd);
}

int64_t early_rootfs_setns(int fd, unsigned flags)
{
	enum ir0_early_rootfs_node node = node_of_fd(fd);

	if (node == IR0_EARLY_ROOTFS_NONE)
		return -EBADF;
	if (node != IR0_EARLY_ROOTFS_PIDFD)
		return -EINVAL;
	if (!flags || (flags & ~IR0_EARLY_CLONE_NS_ALL))
		return -EINVAL;
	return 0;
}

int64_t early_rootfs_pidfd_getfd(int target)
{
	int64_t fd = early_rootfs_dup(target);

	if (fd < 0)
		return fd;
	set_cloexec_of_fd((int)fd, 1);
	return fd;
}

int64_t early_rootfs_kcmp_file(int fd1, int fd2)
{
	enum ir0_early_rootfs_node n1;
	enum ir0_early_rootfs_node n2;

	if (!fd_is_open(fd1) || !fd_is_open(fd2))
		return -EBADF;
	n1 = node_of_fd(fd1);
	n2 = node_of_fd(fd2);
	if (n1 == n2 && pipe_id_of_fd(fd1) == pipe_id_of_fd(fd2))
		return 0;
	return fd1 < fd2 ? 1 : 2;
}

int64_t early_rootfs_faccessat2(int dirfd, uint64_t path, int mode, int flags)
{
	char pbuf[32];

	if (mode & ~7)
		return -EINVAL;
	if (flags & ~IR0_EARLY_FACCESSAT2_FLAGS)
		return -EINVAL;
	if (!g_ready)
		return -ENOENT;
	if (dirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(dirfd) < 0 &&
	    !fd_is_open(dirfd))
		return -EBADF;
	if ((flags & IR0_EARLY_AT_EMPTY_PATH) && path)
	{
		if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
			return -EFAULT;
		if (pbuf[0] == 0)
		{
			if (dirfd == IR0_EARLY_ROOTFS_AT_FDCWD)
				return 0;
			return fd_is_open(dirfd) ? 0 : -EBADF;
		}
	}
	(void)mode;
	return early_rootfs_faccessat(dirfd, path, flags);
}

int64_t early_rootfs_openat2(int dirfd, uint64_t path, uint64_t how,
			     uint64_t usize)
{
	uint64_t blob[3];

	if (usize < IR0_EARLY_OPEN_HOW_BYTES)
		return -EINVAL;
	if (usize > 4096UL)
		return -E2BIG;
	if (!g_user || !g_user->copy_from_user ||
	    g_user->copy_from_user(blob, how, sizeof(blob)) != 0)
		return -EFAULT;
	if (blob[2])
		return -EOPNOTSUPP;
	return early_rootfs_openat(dirfd, path, (int)blob[0]);
}

int early_rootfs_want_creds(int fd)
{
	int uid = unix_id_of_fd(fd);
	int pair = sock_pair_of_fd(fd);
	int end = pipe_id_of_fd(fd) & 1;

	if (uid >= 0 && uid < UNIX_MAX && g_unix[uid].used)
		return g_unix[uid].passcred;
	if (pair >= 0 && pair < SOCKPAIR_MAX && g_sock[pair].used)
		return g_sock[pair].passcred[end];
	return 0;
}

int early_rootfs_take_creds(int fd, int32_t *pid, int32_t *uid, int32_t *gid)
{
	int u = unix_id_of_fd(fd);
	int pair = sock_pair_of_fd(fd);
	int end = pipe_id_of_fd(fd) & 1;
	int src;

	if (!pid || !uid || !gid)
		return 0;
	if (u >= 0 && u < UNIX_MAX && g_unix[u].used &&
	    (g_unix[u].passcred || g_unix[u].cred_explicit) &&
	    g_unix[u].cred_valid)
	{
		*pid = g_unix[u].cred_pid;
		*uid = g_unix[u].cred_uid;
		*gid = g_unix[u].cred_gid;
		g_unix[u].cred_valid = 0;
		g_unix[u].cred_explicit = 0;
		return 1;
	}
	src = 1 - end;
	if (pair >= 0 && pair < SOCKPAIR_MAX && g_sock[pair].used &&
	    (g_sock[pair].passcred[end] || g_sock[pair].cred_explicit[src]) &&
	    g_sock[pair].cred_valid[src])
	{
		*pid = g_sock[pair].cred_pid[src];
		*uid = g_sock[pair].cred_uid[src];
		*gid = g_sock[pair].cred_gid[src];
		g_sock[pair].cred_valid[src] = 0;
		g_sock[pair].cred_explicit[src] = 0;
		return 1;
	}
	if (early_rootfs_want_creds(fd))
	{
		early_process_current_ucred(pid, uid, gid);
		return 1;
	}
	return 0;
}

int64_t early_rootfs_send_creds(int fd, int32_t pid, int32_t uid, int32_t gid)
{
	int32_t cur_pid;
	int32_t cur_uid;
	int32_t cur_gid;
	int u = unix_id_of_fd(fd);
	int pair = sock_pair_of_fd(fd);
	int end = pipe_id_of_fd(fd) & 1;

	early_process_current_ucred(&cur_pid, &cur_uid, &cur_gid);
	if ((pid != cur_pid || uid != cur_uid || gid != cur_gid) &&
	    cur_uid != 0)
		return -EPERM;
	if (u >= 0 && u < UNIX_MAX && g_unix[u].used)
	{
		int dest = g_unix[u].peer_uid >= 0 ? g_unix[u].peer_uid : u;

		note_sender_creds_unix(dest);
		g_unix[dest].cred_explicit = 1;
		return 0;
	}
	if (pair >= 0 && pair < SOCKPAIR_MAX && g_sock[pair].used)
	{
		note_sender_creds_sock(pair, end);
		g_sock[pair].cred_explicit[end] = 1;
		return 0;
	}
	return -ENOTSOCK;
}

int64_t early_rootfs_statx(int dirfd, uint64_t path, int flags, uint32_t mask,
			   uint64_t statxbuf)
{
	struct ir0_early_stat64 st;
	uint8_t sx[IR0_EARLY_STATX_BYTES];
	char pbuf[32];

	(void)mask;
	if (!statxbuf)
		return -EFAULT;
	{
		enum ir0_early_rootfs_node node;
		uint64_t size = g_blob_len;

		if ((flags & IR0_EARLY_AT_EMPTY_PATH) &&
		    copy_user_cstr(path, pbuf, sizeof(pbuf)) && pbuf[0] == '\0')
		{
			node = node_of_fd(dirfd);
			if (node == IR0_EARLY_ROOTFS_NONE)
				return -EBADF;
			if (node == IR0_EARLY_ROOTFS_VDA)
				size = vda_bytes();
			else if (node == IR0_EARLY_ROOTFS_INIT)
				size = g_init_vis;
			else if (node == IR0_EARLY_ROOTFS_MEMFD)
			{
				int mid = pipe_id_of_fd(dirfd);

				size = (mid >= 0 && mid < MEMFD_MAX &&
					g_memfd[mid].used) ?
					       g_memfd[mid].len :
					       0;
			}
			else if (node == IR0_EARLY_ROOTFS_FILE)
			{
				int mid = ramtab_body(pipe_id_of_fd(dirfd));

				size = (mid >= 0 && mid < RAMTAB_MAX &&
					ramtab_table()[mid].used) ?
					       ramtab_table()[mid].size :
					       0;
			}
			ir0_early_rootfs_fill_stat(node, size, &st);
		}
		else
		{
			if (!g_ready)
				return -ENOENT;
			if (dirfd != IR0_EARLY_ROOTFS_AT_FDCWD &&
			    slot_from_fd(dirfd) < 0)
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
			if (node == IR0_EARLY_ROOTFS_VDA)
				size = vda_bytes();
			else if (node == IR0_EARLY_ROOTFS_INIT)
				size = g_init_vis;
			else if (node == IR0_EARLY_ROOTFS_FILE)
			{
				int fsid = ramtab_body(ramtab_find(pbuf));

				size = (fsid >= 0 && ramtab_table()[fsid].used) ?
					       ramtab_table()[fsid].size :
					       0;
			}
			else if (node == IR0_EARLY_ROOTFS_SYMLINK)
			{
				int fsid = ramtab_find(pbuf);

				size = (fsid >= 0) ? ramtab_table()[fsid].size : 0;
			}
			else if (node == IR0_EARLY_ROOTFS_UNIX ||
				 node == IR0_EARLY_ROOTFS_FIFO ||
				 node == IR0_EARLY_ROOTFS_USERDIR)
				size = 0;
			ir0_early_rootfs_fill_stat(node, size, &st);
		}
	}
	ir0_early_statx_from_stat(&st, sx);
	if (!g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(statxbuf, sx, sizeof(sx)) != 0)
		return -EFAULT;
	return 0;
}

int early_rootfs_smoke_init(void)
{
	int fd;
	struct ir0_early_stat64 st;

	if (!g_ready || !g_blob || g_blob_len < 4 || !early_rootfs_console_ready())
		return -1;
	fd = alloc_fd(IR0_EARLY_ROOTFS_INIT);
	if (fd < 0)
		return -1;
	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_INIT, g_blob_len, &st);
	if (st.st_mode != IR0_EARLY_ROOTFS_MODE_REG || st.st_size <= 0 ||
	    g_blob[0] != 0x7f || g_blob[1] != 'E' || g_blob[2] != 'L' ||
	    g_blob[3] != 'F')
	{
		early_rootfs_close(fd);
		return -1;
	}
	early_rootfs_close(fd);
	return 0;
}
