/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel/syscalls/erfs_vfs.c
 * Description: Early rootfs VFS slice — openat, read, block I/O, fcntl, exec, poll.
 */

#include "erfs_internal.h"

int early_rootfs_fd_is_block(int fd)
{
	return node_of_fd(fd) == IR0_EARLY_ROOTFS_VDA;
}

int64_t early_rootfs_block_write(int fd, uint64_t buffer, uint64_t count)
{
	uint64_t *pos = pos_of_fd(fd);
	uint64_t off;
	uint64_t lba;
	uint64_t n;
	uint64_t i;
	uint8_t sector[512];
	uint8_t tmp[256];

	if (!pos || node_of_fd(fd) != IR0_EARLY_ROOTFS_VDA || !vda_present())
		return -EBADF;
	off = *pos;
	if (off >= vda_bytes())
		return -EIO;
	lba = off / 512ULL;
	if (ir0_block_read_by_name("vda", lba, 1, sector) != 0)
		return -EIO;
	n = 512ULL - (off % 512ULL);
	if (n > count)
		n = count;
	if (n > sizeof(tmp))
		n = sizeof(tmp);
	if (!g_user || !g_user->copy_from_user ||
	    g_user->copy_from_user(tmp, buffer, n) != 0)
		return -EFAULT;
	for (i = 0; i < n; i++)
		sector[(off % 512ULL) + i] = tmp[i];
	if (ir0_block_write_by_name("vda", lba, 1, sector) != 0)
		return -EIO;
	*pos = off + n;
	return (int64_t)n;
}
int64_t early_rootfs_openat(int dirfd, uint64_t path, int flags)
{
	char pbuf[32];
	enum ir0_early_rootfs_node node;
	int fd;

	if (!g_ready)
		return -ENOENT;
	if (dirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(dirfd) < 0)
		return -EBADF;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;
	if (!join_under_cwd(pbuf, sizeof(pbuf)))
		return -ENAMETOOLONG;
	node = lookup_user_path(pbuf);
	if (node == IR0_EARLY_ROOTFS_SYMLINK)
	{
		int walked;

		if (flags & LINUX_O_NOFOLLOW)
			return -ELOOP;
		walked = follow_path(pbuf, sizeof(pbuf));
		if (walked < 0)
			return -ELOOP;
		if (!walked)
			return -ENOENT;
		node = lookup_user_path(pbuf);
	}
		if (node == IR0_EARLY_ROOTFS_NONE)
	{
		if (bpath_live() && ir0_bpath_is_foldable(pbuf) &&
		    (flags & O_CREAT))
			node = IR0_EARLY_ROOTFS_FILE;
		else
			return -ENOENT;
	}
if (node == IR0_EARLY_ROOTFS_VDA && !vda_present())
		return -ENOENT;
	if (node == IR0_EARLY_ROOTFS_UNIX)
		return -ENXIO;
	if (node == IR0_EARLY_ROOTFS_SYMLINK)
		return -ELOOP;
	if ((flags & LINUX_O_DIRECTORY) && !node_is_dir(node))
		return -ENOTDIR;
	{
		int denied = landlock_open_need(pbuf, flags);

		if (denied < 0)
			return denied;
	}
	fd = alloc_fd(node);
	if (fd < 0)
		return fd;
	set_flags_of_fd(fd, status_flags(flags));
	set_cloexec_of_fd(fd, (flags & LINUX_O_CLOEXEC) != 0);
	if (bpath_live() && ir0_bpath_is_foldable(pbuf) &&
	    (node == IR0_EARLY_ROOTFS_FILE || node == IR0_EARLY_ROOTFS_USERDIR))
	{
		int64_t prc = product_attach_open(fd, pbuf, flags);

		if (prc < 0)
		{
			close_fd(fd);
			return prc;
		}
		return fd;
	}
	if (node == IR0_EARLY_ROOTFS_FILE || node == IR0_EARLY_ROOTFS_FIFO ||
	    node == IR0_EARLY_ROOTFS_USERDIR)
	{
		int fsid = ramtab_find(pbuf);
		int sidx = slot_from_fd(fd);

		if (fsid >= 0 && sidx >= 0)
			g_slots[sidx].pipe_id = fsid;
		if (node == IR0_EARLY_ROOTFS_FIFO && fsid >= 0)
		{
			int pipe_id = ramtab_table()[fsid].fifo_pipe;

			if (pipe_id < 0)
			{
				for (pipe_id = 0; pipe_id < PIPE_MAX; pipe_id++)
				{
					if (!g_pipes[pipe_id].used)
						break;
				}
				if (pipe_id >= PIPE_MAX)
				{
					close_fd(fd);
					return -EMFILE;
				}
				g_pipes[pipe_id].used = 1;
				g_pipes[pipe_id].rpos = 0;
				g_pipes[pipe_id].len = 0;
				g_pipes[pipe_id].readers = 0;
				g_pipes[pipe_id].writers = 0;
				ramtab_table()[fsid].fifo_pipe = pipe_id;
			}
			if ((flags & LINUX_O_ACCMODE) != 1)
				g_pipes[pipe_id].readers++;
			if ((flags & LINUX_O_ACCMODE) != 0)
				g_pipes[pipe_id].writers++;
		}
	}
	return fd;
}


int64_t early_rootfs_read(int fd, uint64_t buffer, uint64_t count)
{
	enum ir0_early_rootfs_node node;
	int idx;
	struct rootfs_slot *sl;
	uint64_t remain;
	uint64_t n;
	uint64_t i;
	uint8_t tmp[256];

	node = node_of_fd(fd);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -EBADF;
	if (!early_fdtable_ok(fd))
		return -EBADF;
	if (count == 0)
		return 0;
	if (vf_of_fd(fd))
		return product_xfer(fd, buffer, count, 0);
	{
		int64_t via = early_try_vfs_ops_read(fd, node, buffer, count);

		if (via != -ENODEV)
			return via;
	}
	if (node_is_dir(node))
		return -EISDIR;
	if (node == IR0_EARLY_ROOTFS_CONSOLE ||
	    node == IR0_EARLY_ROOTFS_TTY ||
	    node == IR0_EARLY_ROOTFS_NULL)
		return 0;
	if (node == IR0_EARLY_ROOTFS_ZERO)
	{
		n = count > sizeof(tmp) ? sizeof(tmp) : count;
		for (i = 0; i < n; i++)
			tmp[i] = 0;
		if (!g_user || !g_user->copy_to_user ||
		    g_user->copy_to_user(buffer, tmp, n) != 0)
			return -EFAULT;
		return (int64_t)n;
	}
	if (node == IR0_EARLY_ROOTFS_PIPE_W)
		return -EBADF;
	if (node == IR0_EARLY_ROOTFS_PIPE_R)
	{
		int pipe_id = pipe_id_of_fd(fd);
		struct early_pipe *p;
		unsigned take;

		if (pipe_id < 0)
			return -EBADF;
		p = &g_pipes[pipe_id];
		if (!p->used)
			return -EBADF;
		if (p->len == 0)
			return p->writers ? wait_or_eagain(fd) : 0;
		take = p->len;
		if (take > sizeof(tmp))
			take = (unsigned)sizeof(tmp);
		if ((uint64_t)take > count)
			take = (unsigned)count;
		take = pipe_consume(p, tmp, take);
		if (!g_user || !g_user->copy_to_user ||
		    g_user->copy_to_user(buffer, tmp, take) != 0)
			return -EFAULT;
		return (int64_t)take;
	}
	if (node == IR0_EARLY_ROOTFS_VDA)
	{
		uint64_t *pos = pos_of_fd(fd);
		uint64_t off;
		uint64_t lba;
		uint8_t sector[512];

		if (!pos || !vda_present())
			return -EIO;
		off = *pos;
		if (off >= vda_bytes())
			return 0;
		lba = off / 512ULL;
		if (ir0_block_read_by_name("vda", lba, 1, sector) != 0)
			return -EIO;
		n = 512ULL - (off % 512ULL);
		if (n > count)
			n = count;
		if (n > sizeof(tmp))
			n = sizeof(tmp);
		for (i = 0; i < n; i++)
			tmp[i] = sector[(off % 512ULL) + i];
		if (!g_user || !g_user->copy_to_user ||
		    g_user->copy_to_user(buffer, tmp, n) != 0)
			return -EFAULT;
		*pos = off + n;
		return (int64_t)n;
	}
	if (node == IR0_EARLY_ROOTFS_EVENTFD)
	{
		int efd = pipe_id_of_fd(fd);
		uint64_t value;

		if (efd < 0 || efd >= EFD_MAX || !g_eventfd[efd].used)
			return -EBADF;
		if (count < 8)
			return -EINVAL;
		if (g_eventfd[efd].count == 0)
			return (g_eventfd[efd].nonblock ||
				(flags_of_fd(fd) & LINUX_O_NONBLOCK)) ?
				       -EAGAIN :
				       wait_or_eagain(fd);
		if (g_eventfd[efd].semaphore)
		{
			value = 1;
			g_eventfd[efd].count--;
		}
		else
		{
			value = g_eventfd[efd].count;
			g_eventfd[efd].count = 0;
		}
		if (!g_user || !g_user->copy_to_user ||
		    g_user->copy_to_user(buffer, &value, 8) != 0)
			return -EFAULT;
		return 8;
	}
	if (node == IR0_EARLY_ROOTFS_MEMFD)
	{
		int mid = pipe_id_of_fd(fd);
		uint64_t *pos = pos_of_fd(fd);
		uint64_t remain;

		if (mid < 0 || mid >= MEMFD_MAX || !g_memfd[mid].used || !pos)
			return -EBADF;
		if (*pos >= g_memfd[mid].len)
			return 0;
		remain = g_memfd[mid].len - *pos;
		n = count > remain ? remain : count;
		if (n > sizeof(tmp))
			n = sizeof(tmp);
		for (i = 0; i < n; i++)
			tmp[i] = g_memfd[mid].data[*pos + i];
		if (!g_user || !g_user->copy_to_user ||
		    g_user->copy_to_user(buffer, tmp, n) != 0)
			return -EFAULT;
		*pos += n;
		return (int64_t)n;
	}
	if (node == IR0_EARLY_ROOTFS_TIMERFD)
	{
		int tid = pipe_id_of_fd(fd);
		uint64_t value;

		if (tid < 0 || tid >= TIMERFD_MAX || !g_timerfd[tid].used)
			return -EBADF;
		if (count < 8)
			return -EINVAL;
		timerfd_advance_one(&g_timerfd[tid]);
		if (g_timerfd[tid].ticks == 0)
			return -EAGAIN;
		value = g_timerfd[tid].ticks;
		g_timerfd[tid].ticks = 0;
		if (!g_user || !g_user->copy_to_user ||
		    g_user->copy_to_user(buffer, &value, 8) != 0)
			return -EFAULT;
		return 8;
	}
	if (node == IR0_EARLY_ROOTFS_SOCK)
	{
		int uid = unix_id_of_fd(fd);
		int pair = pipe_id_of_fd(fd) >> 1;
		int end = pipe_id_of_fd(fd) & 1;
		int src;
		unsigned take;
		unsigned i2;

		if (uid >= 0)
		{
			if (!g_unix[uid].used)
				return -EBADF;
			if (g_unix[uid].stream)
				return -ENOTCONN;
			if (g_unix[uid].len == 0)
				return wait_or_eagain(fd);
			if (g_unix[uid].len < 2)
				return -EIO;
			{
				uint16_t mlen;
				uint8_t hdr[2];
				uint8_t pkt[256];

				if (early_unix_peek(&g_unix[uid], hdr, 2) < 2)
					return -EIO;
				mlen = (uint16_t)hdr[0] | ((uint16_t)hdr[1] << 8);
				if (g_unix[uid].len < 2U + mlen || mlen > 250)
					return -EIO;
				early_unix_consume(&g_unix[uid], pkt, 2U + mlen);
				take = mlen;
				if (take > sizeof(tmp))
					take = (unsigned)sizeof(tmp);
				if ((uint64_t)take > count)
					take = (unsigned)count;
				for (i2 = 0; i2 < take; i2++)
					tmp[i2] = pkt[2 + i2];
				if (!g_user || !g_user->copy_to_user ||
				    g_user->copy_to_user(buffer, tmp, take) != 0)
					return -EFAULT;
				return (int64_t)take;
			}
		}
		if (pair < 0 || pair >= SOCKPAIR_MAX || !g_sock[pair].used)
			return -EBADF;
		src = 1 - end;
		if (g_sock[pair].shut[end] & 1)
			return 0;
		if (g_sock[pair].dir[src].len == 0)
			return (g_sock[pair].refs[src] &&
				!(g_sock[pair].shut[src] & 2)) ?
				       wait_or_eagain(fd) :
				       0;
		if (g_sock[pair].dgram)
		{
			uint16_t mlen;
			uint8_t hdr[2];
			uint8_t pkt[256];

			if (g_sock[pair].dir[src].len < 2)
				return -EIO;
			if (early_sock_peek(&g_sock[pair], src, hdr, 2) < 2)
				return -EIO;
			mlen = (uint16_t)hdr[0] | ((uint16_t)hdr[1] << 8);
			if (g_sock[pair].dir[src].len < 2U + mlen || mlen > 250)
				return -EIO;
			early_sock_consume(&g_sock[pair], src, pkt, 2U + mlen);
			take = mlen;
			if (take > sizeof(tmp))
				take = (unsigned)sizeof(tmp);
			if ((uint64_t)take > count)
				take = (unsigned)count;
			for (i2 = 0; i2 < take; i2++)
				tmp[i2] = pkt[2 + i2];
			if (!g_user || !g_user->copy_to_user ||
			    g_user->copy_to_user(buffer, tmp, take) != 0)
				return -EFAULT;
			return (int64_t)take;
		}
		take = g_sock[pair].dir[src].len;
		if (take > sizeof(tmp))
			take = (unsigned)sizeof(tmp);
		if ((uint64_t)take > count)
			take = (unsigned)count;
		take = early_sock_consume(&g_sock[pair], src, tmp, take);
		if (!g_user || !g_user->copy_to_user ||
		    g_user->copy_to_user(buffer, tmp, take) != 0)
			return -EFAULT;
		return (int64_t)take;
	}
	if (node == IR0_EARLY_ROOTFS_EPOLL)
		return -EINVAL;
	if (node == IR0_EARLY_ROOTFS_FILE)
	{
		int fsid = ramtab_body(pipe_id_of_fd(fd));
		uint64_t *pos = pos_of_fd(fd);
		uint64_t remain;

		if (fsid < 0 || fsid >= RAMTAB_MAX || !ramtab_table()[fsid].used || !pos)
			return -EBADF;
		if (*pos >= ramtab_table()[fsid].size)
			return 0;
		remain = ramtab_table()[fsid].size - *pos;
		n = count > remain ? remain : count;
		if (n > sizeof(tmp))
			n = sizeof(tmp);
		for (i = 0; i < n; i++)
			tmp[i] = ramtab_table()[fsid].data ? ramtab_table()[fsid].data[*pos + i] : 0;
		if (!g_user || !g_user->copy_to_user ||
		    g_user->copy_to_user(buffer, tmp, n) != 0)
			return -EFAULT;
		*pos += n;
		return (int64_t)n;
	}
	if (node == IR0_EARLY_ROOTFS_FIFO)
	{
		int fsid = pipe_id_of_fd(fd);
		int pipe_id;
		struct early_pipe *p;
		unsigned take;

		if ((flags_of_fd(fd) & LINUX_O_ACCMODE) == 1)
			return -EBADF;
		if (fsid < 0 || fsid >= RAMTAB_MAX || !ramtab_table()[fsid].used)
			return -EBADF;
		pipe_id = ramtab_table()[fsid].fifo_pipe;
		if (pipe_id < 0 || pipe_id >= PIPE_MAX)
			return -EBADF;
		p = &g_pipes[pipe_id];
		if (!p->used)
			return -EBADF;
		if (p->len == 0)
			return p->writers ? wait_or_eagain(fd) : 0;
		take = p->len;
		if (take > sizeof(tmp))
			take = (unsigned)sizeof(tmp);
		if ((uint64_t)take > count)
			take = (unsigned)count;
		take = pipe_consume(p, tmp, take);
		if (!g_user || !g_user->copy_to_user ||
		    g_user->copy_to_user(buffer, tmp, take) != 0)
			return -EFAULT;
		return (int64_t)take;
	}
	idx = slot_from_fd(fd);
	if (idx < 0)
		return -EBADF;
	sl = &g_slots[idx];
	if (!g_blob || sl->pos >= g_init_vis)
		return 0;
	remain = g_init_vis - sl->pos;
	if (sl->pos >= g_blob_len)
		return 0;
	if (remain > g_blob_len - sl->pos)
		remain = g_blob_len - sl->pos;
	n = count > remain ? remain : count;
	if (n > sizeof(tmp))
		n = sizeof(tmp);
	for (i = 0; i < n; i++)
		tmp[i] = g_blob[sl->pos + i];
	if (!g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(buffer, tmp, n) != 0)
		return -EFAULT;
	sl->pos += n;
	return (int64_t)n;
}

int early_rootfs_fd_is_console(int fd)
{
	enum ir0_early_rootfs_node node;

	if (!g_ready && fd >= 0 && fd <= 2)
		return 1;
	node = node_of_fd(fd);
	return node == IR0_EARLY_ROOTFS_CONSOLE ||
	       node == IR0_EARLY_ROOTFS_TTY;
}

int early_rootfs_fd_discards_write(int fd)
{
	enum ir0_early_rootfs_node node = node_of_fd(fd);

	return node == IR0_EARLY_ROOTFS_NULL ||
	       node == IR0_EARLY_ROOTFS_ZERO;
}

int64_t early_rootfs_fcntl(int fd, int cmd, uint64_t arg)
{
	if (!fd_is_open(fd))
		return -EBADF;
	if (cmd == F_GETFD)
		return cloexec_of_fd(fd) ? FD_CLOEXEC : 0;
	if (cmd == F_SETFD)
	{
		if (arg & ~(uint64_t)FD_CLOEXEC)
			return -EINVAL;
		set_cloexec_of_fd(fd, (int)arg);
		return 0;
	}
	if (cmd == F_GETFL)
		return flags_of_fd(fd);
	if (cmd == F_SETFL)
	{
		set_flags_of_fd(fd, (flags_of_fd(fd) & LINUX_O_ACCMODE) |
				    status_flags((int)arg));
		return 0;
	}
	if (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC)
	{
		if ((int64_t)arg < 0 || (int)arg >= ROOTFS_FD_LIMIT)
			return -EINVAL;
		return early_rootfs_fcntl_dupfd(fd, (int)arg,
						cmd == F_DUPFD_CLOEXEC);
	}
	if (cmd == F_GETLK || cmd == F_SETLK || cmd == F_SETLKW)
		return -EINVAL;
	return -EINVAL;
}

int64_t early_rootfs_fsync(int fd)
{
	if (!fd_is_open(fd))
		return -EBADF;
	return 0;
}

static int walk_user_vector(uint64_t user_vec, int store)
{
	unsigned i;
	uint64_t ptr;
	char discard[EXEC_ARG_LEN];

	if (!user_vec || !g_user || !g_user->copy_from_user)
		return -EFAULT;
	for (i = 0; i < EXEC_ARGC_MAX; i++)
	{
		if (g_user->copy_from_user(&ptr, user_vec + (uint64_t)i * 8UL,
					   sizeof(ptr)) != 0)
			return -EFAULT;
		if (!ptr)
		{
			if (store)
				g_exec_argv[i] = 0;
			return 0;
		}
		if (!copy_user_cstr(ptr, store ? g_exec_args[i] : discard,
				    EXEC_ARG_LEN))
			return -EFAULT;
		if (store)
			g_exec_argv[i] = g_exec_args[i];
	}
	if (g_user->copy_from_user(&ptr, user_vec + (uint64_t)EXEC_ARGC_MAX * 8UL,
				   sizeof(ptr)) != 0)
		return -EFAULT;
	if (ptr)
		return -E2BIG;
	if (store)
		g_exec_argv[EXEC_ARGC_MAX] = 0;
	return 0;
}

int64_t early_rootfs_execve(uint64_t path, uint64_t argv, uint64_t envp)
{
	char pbuf[32];
	enum ir0_early_rootfs_node node;
	int ret;

	if (!g_ready || !g_blob)
		return -ENOENT;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;
	node = lookup_user_path(pbuf);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -ENOENT;
	if (node != IR0_EARLY_ROOTFS_INIT)
		return -EACCES;
	{
		int denied = landlock_allow(pbuf, IR0_EARLY_LANDLOCK_FS_EXECUTE);

		if (denied < 0)
			return denied;
	}
	ret = walk_user_vector(envp, 0);
	if (ret < 0)
		return ret;
	ret = walk_user_vector(argv, 1);
	if (ret < 0)
		return ret;
	g_exec_pending = 1;
	g_exec_owner = (int)early_process_current_pid();
	return 0;
}

int early_rootfs_exec_pending(void)
{
	return g_exec_pending &&
	       (int)early_process_current_pid() == g_exec_owner;
}

int early_rootfs_abstract_observed(void)
{
	return g_abstract_ok;
}

char *const *early_rootfs_exec_argv(void)
{
	return g_exec_argv;
}

void early_rootfs_exec_clear(void)
{
	g_exec_pending = 0;
	g_exec_owner = 0;
}

int early_rootfs_fd_is_pipe_write(int fd)
{
	return node_of_fd(fd) == IR0_EARLY_ROOTFS_PIPE_W;
}

int early_rootfs_fd_is_eventfd(int fd)
{
	return node_of_fd(fd) == IR0_EARLY_ROOTFS_EVENTFD;
}

int early_rootfs_fd_is_memfd(int fd)
{
	return node_of_fd(fd) == IR0_EARLY_ROOTFS_MEMFD;
}

int early_rootfs_fd_is_sock(int fd)
{
	return node_of_fd(fd) == IR0_EARLY_ROOTFS_SOCK;
}

int early_rootfs_fd_is_file(int fd)
{
	return node_of_fd(fd) == IR0_EARLY_ROOTFS_FILE;
}

int early_rootfs_fd_is_fifo(int fd)
{
	return node_of_fd(fd) == IR0_EARLY_ROOTFS_FIFO;
}

int early_rootfs_poll_revents(int fd, int events)
{
	enum ir0_early_rootfs_node node;
	int pipe_id;
	int ready = 0;

	if (fd < 0)
		return 0;
	if (!fd_is_open(fd))
		return IR0_EARLY_POLLNVAL;
	node = node_of_fd(fd);
	pipe_id = pipe_id_of_fd(fd);
	if (node == IR0_EARLY_ROOTFS_PIPE_R)
	{
		struct early_pipe *p = (pipe_id >= 0) ? &g_pipes[pipe_id] : 0;

		if (!p || !p->used)
			return IR0_EARLY_POLLNVAL;
		if (p->len > 0)
			ready |= IR0_EARLY_POLLIN;
		if (p->writers == 0)
			ready |= IR0_EARLY_POLLHUP;
		return ready & (events | IR0_EARLY_POLLHUP | IR0_EARLY_POLLERR);
	}
	if (node == IR0_EARLY_ROOTFS_PIPE_W)
	{
		struct early_pipe *p = (pipe_id >= 0) ? &g_pipes[pipe_id] : 0;

		if (!p || !p->used)
			return IR0_EARLY_POLLNVAL;
		if (p->len < PIPE_BUF)
			ready |= IR0_EARLY_POLLOUT;
		if (p->readers == 0)
			ready |= IR0_EARLY_POLLERR;
		return ready & (events | IR0_EARLY_POLLHUP | IR0_EARLY_POLLERR);
	}
	if (node == IR0_EARLY_ROOTFS_EVENTFD)
	{
		int efd = pipe_id_of_fd(fd);

		if (efd < 0 || efd >= EFD_MAX || !g_eventfd[efd].used)
			return IR0_EARLY_POLLNVAL;
		if (g_eventfd[efd].count > 0)
			ready |= IR0_EARLY_POLLIN;
		if (g_eventfd[efd].count < EFD_COUNT_MAX)
			ready |= IR0_EARLY_POLLOUT;
		return ready & events;
	}
	if (node == IR0_EARLY_ROOTFS_TIMERFD)
	{
		int tid = pipe_id_of_fd(fd);

		if (tid < 0 || tid >= TIMERFD_MAX || !g_timerfd[tid].used)
			return IR0_EARLY_POLLNVAL;
		timerfd_advance_one(&g_timerfd[tid]);
		if (g_timerfd[tid].ticks > 0)
			ready |= IR0_EARLY_POLLIN;
		return ready & events;
	}
	if (node == IR0_EARLY_ROOTFS_MEMFD)
		ready = IR0_EARLY_POLLIN | IR0_EARLY_POLLOUT;
	else if (node == IR0_EARLY_ROOTFS_SOCK)
	{
		int uid = unix_id_of_fd(fd);
		int pair = pipe_id >> 1;
		int end = pipe_id & 1;
		int src = 1 - end;

		if (uid >= 0)
		{
			if (!g_unix[uid].used)
				return IR0_EARLY_POLLNVAL;
			if (g_unix[uid].listening && g_unix[uid].pending_pair >= 0)
				ready |= IR0_EARLY_POLLIN;
			if (!g_unix[uid].stream)
			{
				if (g_unix[uid].len > 0)
					ready |= IR0_EARLY_POLLIN;
				if (g_unix[uid].len + 2U < PIPE_BUF)
					ready |= IR0_EARLY_POLLOUT;
			}
			else if (!g_unix[uid].listening)
				ready |= IR0_EARLY_POLLOUT;
			return ready & (events | IR0_EARLY_POLLHUP);
		}
		if (pair < 0 || pair >= SOCKPAIR_MAX || !g_sock[pair].used)
			return IR0_EARLY_POLLNVAL;
		if (g_sock[pair].dir[src].len > 0)
			ready |= IR0_EARLY_POLLIN;
		if (g_sock[pair].dir[end].len < PIPE_BUF)
			ready |= IR0_EARLY_POLLOUT;
		if (g_sock[pair].refs[src] == 0)
			ready |= IR0_EARLY_POLLHUP;
		return ready & (events | IR0_EARLY_POLLHUP | IR0_EARLY_POLLERR);
	}
	else if (node == IR0_EARLY_ROOTFS_EPOLL)
	{
		int eid = pipe_id;
		unsigned s;

		if (eid < 0 || eid >= EPOLL_MAX || !g_epoll[eid].used)
			return IR0_EARLY_POLLNVAL;
		for (s = 0; s < EPOLL_SLOTS; s++)
		{
			if (!g_epoll[eid].slot[s].used)
				continue;
			if (node_of_fd(g_epoll[eid].slot[s].fd) ==
			    IR0_EARLY_ROOTFS_EPOLL)
				continue;
			if (early_rootfs_poll_revents(g_epoll[eid].slot[s].fd,
						      (int)g_epoll[eid].slot[s].events))
			{
				ready |= IR0_EARLY_POLLIN;
				break;
			}
		}
		return ready & events;
	}
	if (node == IR0_EARLY_ROOTFS_CONSOLE || node == IR0_EARLY_ROOTFS_TTY)
		ready = IR0_EARLY_POLLOUT;
	else if (node == IR0_EARLY_ROOTFS_NULL || node == IR0_EARLY_ROOTFS_ZERO ||
		 node == IR0_EARLY_ROOTFS_VDA || node == IR0_EARLY_ROOTFS_INIT ||
		 node == IR0_EARLY_ROOTFS_FILE)
		ready = IR0_EARLY_POLLIN | IR0_EARLY_POLLOUT;
	else if (node == IR0_EARLY_ROOTFS_FIFO)
	{
		int fsid = pipe_id;
		int fifo;

		if (fsid < 0 || fsid >= RAMTAB_MAX || !ramtab_table()[fsid].used)
			return IR0_EARLY_POLLNVAL;
		fifo = ramtab_table()[fsid].fifo_pipe;
		if (fifo < 0 || fifo >= PIPE_MAX || !g_pipes[fifo].used)
			return 0;
		if (g_pipes[fifo].len > 0)
			ready |= IR0_EARLY_POLLIN;
		if (g_pipes[fifo].len < PIPE_BUF)
			ready |= IR0_EARLY_POLLOUT;
		if (g_pipes[fifo].writers == 0)
			ready |= IR0_EARLY_POLLHUP;
		return ready & (events | IR0_EARLY_POLLHUP | IR0_EARLY_POLLERR);
	}
	else if (node_is_dir(node))
		ready = IR0_EARLY_POLLIN;
	return ready & (events | IR0_EARLY_POLLERR | IR0_EARLY_POLLHUP);
}
