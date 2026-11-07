/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel/syscalls/erfs_meta.c
 * Description: early metadata, rename and mount syscalls (xattr/handles in
 *              early_rootfs_xattr.c; Landlock in early_rootfs_landlock.c).
 */

#include "erfs_internal.h"

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
