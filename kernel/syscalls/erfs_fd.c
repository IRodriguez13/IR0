/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel/syscalls/erfs_fd.c
 * Description: Early rootfs descriptor table — close, install, dup, fdtable hooks.
 */

#include "erfs_internal.h"

static void early_publish_fd(int fd, enum ir0_early_rootfs_node node);
static const char *early_node_path(enum ir0_early_rootfs_node node);

static void attach_object(int oldfd, int newfd)
{
	int pipe_id = pipe_id_of_fd(oldfd);
	int nidx = slot_from_fd(newfd);
	enum ir0_early_rootfs_node node = node_of_fd(oldfd);

	if (nidx >= 0)
		g_slots[nidx].pipe_id = pipe_id;
	if (pipe_id >= 0 && pipe_id < PIPE_MAX && g_pipes[pipe_id].used)
	{
		if (node == IR0_EARLY_ROOTFS_PIPE_R)
			g_pipes[pipe_id].readers++;
		if (node == IR0_EARLY_ROOTFS_PIPE_W)
			g_pipes[pipe_id].writers++;
	}
	if (node == IR0_EARLY_ROOTFS_MEMFD &&
	    pipe_id >= 0 && pipe_id < MEMFD_MAX && g_memfd[pipe_id].used)
		g_memfd[pipe_id].refs++;
	if (node == IR0_EARLY_ROOTFS_TIMERFD &&
	    pipe_id >= 0 && pipe_id < TIMERFD_MAX && g_timerfd[pipe_id].used)
		g_timerfd[pipe_id].refs++;
	if (node == IR0_EARLY_ROOTFS_EPOLL &&
	    pipe_id >= 0 && pipe_id < EPOLL_MAX && g_epoll[pipe_id].used)
		g_epoll[pipe_id].refs++;
	if (node == IR0_EARLY_ROOTFS_SOCK)
	{
		int uid = unix_id_of_fd(oldfd);
		int pair = pipe_id >> 1;
		int end = pipe_id & 1;

		if (uid >= 0 && uid < UNIX_MAX && g_unix[uid].used)
			g_unix[uid].refs++;
		else if (pair >= 0 && pair < SOCKPAIR_MAX && g_sock[pair].used)
			g_sock[pair].refs[end]++;
	}
}

int close_fd(int fd)
{
	int idx;

	if (fd >= 0 && fd <= 2)
	{
		if (!g_stdio_open[fd])
			return -EBADF;
		g_stdio_open[fd] = 0;
		g_stdio_node[fd] = IR0_EARLY_ROOTFS_NONE;
		g_stdio_pos[fd] = 0;
		g_stdio_flags[fd] = 0;
		g_stdio_cloexec[fd] = 0;
		if (g_fd_ops && g_fd_ops->close)
			(void)g_fd_ops->close(fd);
		return 0;
	}
	idx = slot_from_fd(fd);
	if (idx < 0)
		return -EBADF;
	{
		int pipe_id = g_slots[idx].pipe_id;
		enum ir0_early_rootfs_node node = g_slots[idx].node;

		if (pipe_id >= 0 && pipe_id < PIPE_MAX && g_pipes[pipe_id].used)
		{
			if (node == IR0_EARLY_ROOTFS_PIPE_R &&
			    g_pipes[pipe_id].readers > 0)
				g_pipes[pipe_id].readers--;
			if (node == IR0_EARLY_ROOTFS_PIPE_W &&
			    g_pipes[pipe_id].writers > 0)
				g_pipes[pipe_id].writers--;
			if (g_pipes[pipe_id].readers == 0 &&
			    g_pipes[pipe_id].writers == 0)
				g_pipes[pipe_id].used = 0;
		}
		if (node == IR0_EARLY_ROOTFS_EVENTFD &&
		    pipe_id >= 0 && pipe_id < EFD_MAX)
			g_eventfd[pipe_id].used = 0;
		if (node == IR0_EARLY_ROOTFS_MEMFD &&
		    pipe_id >= 0 && pipe_id < MEMFD_MAX && g_memfd[pipe_id].used)
		{
			if (g_memfd[pipe_id].refs > 0)
				g_memfd[pipe_id].refs--;
			if (g_memfd[pipe_id].refs == 0)
				g_memfd[pipe_id].used = 0;
		}
		if (node == IR0_EARLY_ROOTFS_TIMERFD &&
		    pipe_id >= 0 && pipe_id < TIMERFD_MAX &&
		    g_timerfd[pipe_id].used)
		{
			if (g_timerfd[pipe_id].refs > 0)
				g_timerfd[pipe_id].refs--;
			if (g_timerfd[pipe_id].refs == 0)
				g_timerfd[pipe_id].used = 0;
		}
		if (node == IR0_EARLY_ROOTFS_EPOLL &&
		    pipe_id >= 0 && pipe_id < EPOLL_MAX && g_epoll[pipe_id].used)
		{
			if (g_epoll[pipe_id].refs > 0)
				g_epoll[pipe_id].refs--;
			if (g_epoll[pipe_id].refs == 0)
				g_epoll[pipe_id].used = 0;
		}
		if (node == IR0_EARLY_ROOTFS_FIFO && pipe_id >= 0 &&
		    pipe_id < RAMTAB_MAX && ramtab_table()[pipe_id].used)
		{
			int fifo = ramtab_table()[pipe_id].fifo_pipe;
			int acc = g_slots[idx].flags & LINUX_O_ACCMODE;

			if (fifo >= 0 && fifo < PIPE_MAX && g_pipes[fifo].used)
			{
				if (acc != 1 && g_pipes[fifo].readers > 0)
					g_pipes[fifo].readers--;
				if (acc != 0 && g_pipes[fifo].writers > 0)
					g_pipes[fifo].writers--;
				if (g_pipes[fifo].readers == 0 &&
				    g_pipes[fifo].writers == 0)
				{
					g_pipes[fifo].used = 0;
					ramtab_table()[pipe_id].fifo_pipe = -1;
				}
			}
		}
		if (node == IR0_EARLY_ROOTFS_RULESET && pipe_id >= 0 &&
		    pipe_id < RULESET_MAX && g_ruleset[pipe_id].used)
		{
			if (g_ruleset[pipe_id].refs > 0)
				g_ruleset[pipe_id].refs--;
			if (g_ruleset[pipe_id].refs == 0)
				g_ruleset[pipe_id].used = 0;
		}
		if (node == IR0_EARLY_ROOTFS_SOCK)
		{
			int uid = unix_id_of_fd(fd);
			int pair = pipe_id >> 1;
			int end = pipe_id & 1;

			if (uid >= 0 && uid < UNIX_MAX && g_unix[uid].used)
			{
				if (g_unix[uid].refs > 0)
					g_unix[uid].refs--;
				if (g_unix[uid].refs == 0)
				{
					if (g_unix[uid].pending_pair >= 0 &&
					    g_unix[uid].pending_pair < SOCKPAIR_MAX &&
					    g_sock[g_unix[uid].pending_pair].used)
					{
						int pend = g_unix[uid].pending_pair;
						int pend_end = g_unix[uid].pending_end;

						if (g_sock[pend].refs[pend_end] > 0)
							g_sock[pend].refs[pend_end]--;
						if (g_sock[pend].refs[0] == 0 &&
						    g_sock[pend].refs[1] == 0)
							g_sock[pend].used = 0;
					}
					g_unix[uid].used = 0;
				}
			}
			else if (pair >= 0 && pair < SOCKPAIR_MAX &&
				 g_sock[pair].used && end >= 0 && end <= 1)
			{
				if (g_sock[pair].refs[end] > 0)
					g_sock[pair].refs[end]--;
				if (g_sock[pair].refs[0] == 0 &&
				    g_sock[pair].refs[1] == 0)
					g_sock[pair].used = 0;
			}
		}
	}
	if (g_slots[idx].vf && g_product_path && g_product_path->put)
		(void)g_product_path->put(g_slots[idx].vf);
	g_slots[idx].vf = 0;
	g_slots[idx].used = 0;
	g_slots[idx].node = IR0_EARLY_ROOTFS_NONE;
	g_slots[idx].pos = 0;
	g_slots[idx].flags = 0;
	g_slots[idx].cloexec = 0;
	g_slots[idx].pipe_id = -1;
	if (g_fd_ops && g_fd_ops->close)
		(void)g_fd_ops->close(fd);
	return 0;
}

static int install_fd(int fd, enum ir0_early_rootfs_node node)
{
	unsigned idx;

	if (fd >= 0 && fd <= 2)
	{
		g_stdio_open[fd] = 1;
		g_stdio_node[fd] = node;
		g_stdio_pos[fd] = 0;
		g_stdio_flags[fd] = O_RDWR;
		g_stdio_cloexec[fd] = 0;
		early_publish_fd(fd, node);
		return fd;
	}
	if (fd < ROOTFS_FD_BASE || fd >= ROOTFS_FD_LIMIT)
		return -EBADF;
	idx = (unsigned)(fd - ROOTFS_FD_BASE);
	g_slots[idx].used = 1;
	g_slots[idx].node = node;
	g_slots[idx].pos = 0;
	g_slots[idx].flags = O_RDWR;
	g_slots[idx].cloexec = 0;
	g_slots[idx].pipe_id = -1;
	early_publish_fd(fd, node);
	return fd;
}

static int lowest_unused_fd(void)
{
	int fd;

	for (fd = 0; fd < ROOTFS_FD_LIMIT; fd++)
	{
		if (!fd_is_open(fd))
			return fd;
	}
	return -EMFILE;
}

int alloc_fd(enum ir0_early_rootfs_node node)
{
	unsigned i;

	for (i = 0; i < ROOTFS_MAX_OPEN; i++)
	{
		if (!g_slots[i].used)
		{
			g_slots[i].used = 1;
			g_slots[i].node = node;
			g_slots[i].pos = 0;
			g_slots[i].flags = O_RDWR;
			g_slots[i].cloexec = 0;
			g_slots[i].pipe_id = -1;
			g_slots[i].vf = 0;
			{
				int fd = (int)(ROOTFS_FD_BASE + i);

				early_publish_fd(fd, node);
				return fd;
			}
		}
	}
	return -EMFILE;
}

static const char *early_node_path(enum ir0_early_rootfs_node node)
{
	if (node == IR0_EARLY_ROOTFS_ROOT)
		return "/";
	if (node == IR0_EARLY_ROOTFS_INIT)
		return "/init";
	if (node == IR0_EARLY_ROOTFS_DEV)
		return "/dev";
	if (node == IR0_EARLY_ROOTFS_VDA)
		return "/dev/vda";
	if (node == IR0_EARLY_ROOTFS_TMP)
		return "/tmp";
	if (node == IR0_EARLY_ROOTFS_CONSOLE)
		return "/dev/console";
	if (node == IR0_EARLY_ROOTFS_NULL)
		return "/dev/null";
	if (node == IR0_EARLY_ROOTFS_ZERO)
		return "/dev/zero";
	if (node == IR0_EARLY_ROOTFS_TTY)
		return "/dev/tty";
	return "";
}

static void early_publish_fd(int fd, enum ir0_early_rootfs_node node)
{
	int flags;

	if (fd < 0 || !g_fd_ops || !g_fd_ops->install)
		return;
	flags = flags_of_fd(fd);
	if (g_fd_ops->install(fd, early_node_path(node), flags) == 0)
	{
		g_fd_used = 1;
		if (g_fd_ops->file && g_fd_ops->file(fd))
			g_vfs_file_used = 1;
	}
}

void early_rootfs_bind_fdtable(const struct ir0_fd_install_ops *ops)
{
	unsigned i;

	g_fd_ops = ops;
	g_fd_used = 0;
	g_vfs_file_used = 0;
	if (!ops || !ops->install)
		return;
	for (i = 0; i < 3; i++)
	{
		if (g_stdio_open[i])
			early_publish_fd((int)i, g_stdio_node[i]);
	}
	for (i = 0; i < ROOTFS_MAX_OPEN; i++)
	{
		if (g_slots[i].used)
			early_publish_fd((int)(ROOTFS_FD_BASE + i),
					 g_slots[i].node);
	}
}

int early_rootfs_fdtable_used(void)
{
	return g_fd_used;
}

int early_rootfs_vfs_file_used(void)
{
	return g_vfs_file_used;
}

int early_fdtable_ok(int fd)
{
	if (!g_fd_ops || !g_fd_ops->check)
		return 1;
	return g_fd_ops->check(fd) == 0;
}

int cloexec_of_fd(int fd)
{
	int idx;

	if (fd >= 0 && fd <= 2)
		return g_stdio_open[fd] ? g_stdio_cloexec[fd] : 0;
	idx = slot_from_fd(fd);
	if (idx < 0)
		return 0;
	return g_slots[idx].cloexec;
}

int64_t early_rootfs_close(int fd)
{
	return close_fd(fd);
}

int64_t early_rootfs_dup(int oldfd)
{
	int newfd;

	if (!g_ready || !fd_is_open(oldfd))
		return -EBADF;
	newfd = lowest_unused_fd();
	if (newfd < 0)
		return newfd;
	if (install_fd(newfd, node_of_fd(oldfd)) < 0)
		return -EBADF;
	set_flags_of_fd(newfd, flags_of_fd(oldfd));
	set_cloexec_of_fd(newfd, cloexec_of_fd(oldfd));
	attach_object(oldfd, newfd);
	return newfd;
}

int64_t early_rootfs_dup3(int oldfd, int newfd, int flags)
{
	if (oldfd == newfd)
		return -EINVAL;
	if (flags & ~LINUX_O_CLOEXEC)
		return -EINVAL;
	if (!g_ready || !fd_is_open(oldfd))
		return -EBADF;
	if (newfd < 0 || newfd >= ROOTFS_FD_LIMIT)
		return -EBADF;
	if (fd_is_open(newfd) && close_fd(newfd) != 0)
		return -EBADF;
	if (install_fd(newfd, node_of_fd(oldfd)) < 0)
		return -EBADF;
	set_flags_of_fd(newfd, flags_of_fd(oldfd));
	set_cloexec_of_fd(newfd, (flags & LINUX_O_CLOEXEC) != 0);
	attach_object(oldfd, newfd);
	return newfd;
}

int64_t early_rootfs_fcntl_dupfd(int fd, int minfd, int cloexec)
{
	int newfd;

	if (!fd_is_open(fd))
		return -EBADF;
	if (minfd < 0 || minfd >= ROOTFS_FD_LIMIT)
		return -EINVAL;
	for (newfd = minfd; newfd < ROOTFS_FD_LIMIT; newfd++)
	{
		if (fd_is_open(newfd))
			continue;
		if (install_fd(newfd, node_of_fd(fd)) < 0)
			return -EBADF;
		set_flags_of_fd(newfd, flags_of_fd(fd));
		set_cloexec_of_fd(newfd, cloexec);
		attach_object(fd, newfd);
		return newfd;
	}
	return -EMFILE;
}
