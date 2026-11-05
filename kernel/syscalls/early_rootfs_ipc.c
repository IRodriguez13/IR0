/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel/syscalls/early_rootfs_ipc.c
 * Description: early pipes, eventfd, memfd, timerfd and epoll.
 */

#include "early_rootfs_internal.h"

int64_t early_rootfs_pipe_write(int fd, uint64_t buffer, uint64_t count)
{
	int pipe_id = pipe_id_of_fd(fd);
	struct early_pipe *p;
	uint8_t tmp[256];
	unsigned room;
	unsigned put;

	if (pipe_id < 0 || node_of_fd(fd) != IR0_EARLY_ROOTFS_PIPE_W)
		return -EBADF;
	p = &g_pipes[pipe_id];
	if (!p->used)
		return -EBADF;
	if (p->readers == 0)
		return -EPIPE;
	if (count == 0)
		return 0;
	room = PIPE_BUF - p->len;
	if (room == 0)
		return wait_or_eagain(fd);
	put = room;
	if (put > sizeof(tmp))
		put = (unsigned)sizeof(tmp);
	if ((uint64_t)put > count)
		put = (unsigned)count;
	if (!g_user || !g_user->copy_from_user ||
	    g_user->copy_from_user(tmp, buffer, put) != 0)
		return -EFAULT;
	put = pipe_deposit(p, tmp, put);
	return (int64_t)put;
}

int64_t early_rootfs_pipe2(uint64_t fds, int flags)
{
	int pipe_id;
	int rfd;
	int wfd;
	int pair[2];
	int ridx;
	int widx;

	if (!g_ready)
		return -ENOSYS;
	if (flags & ~(LINUX_O_CLOEXEC | LINUX_O_NONBLOCK))
		return -EINVAL;
	if (!g_user || !g_user->copy_to_user)
		return -EFAULT;
	for (pipe_id = 0; pipe_id < PIPE_MAX; pipe_id++)
	{
		if (!g_pipes[pipe_id].used)
			break;
	}
	if (pipe_id >= PIPE_MAX)
		return -EMFILE;
	rfd = alloc_fd(IR0_EARLY_ROOTFS_PIPE_R);
	if (rfd < 0)
		return rfd;
	wfd = alloc_fd(IR0_EARLY_ROOTFS_PIPE_W);
	if (wfd < 0)
	{
		close_fd(rfd);
		return wfd;
	}
	ridx = slot_from_fd(rfd);
	widx = slot_from_fd(wfd);
	if (ridx < 0 || widx < 0)
	{
		close_fd(rfd);
		close_fd(wfd);
		return -EBADF;
	}
	g_pipes[pipe_id].used = 1;
	g_pipes[pipe_id].rpos = 0;
	g_pipes[pipe_id].len = 0;
	g_pipes[pipe_id].readers = 1;
	g_pipes[pipe_id].writers = 1;
	g_slots[ridx].pipe_id = pipe_id;
	g_slots[widx].pipe_id = pipe_id;
	set_flags_of_fd(rfd, status_flags(flags) | 0);
	set_flags_of_fd(wfd, status_flags(flags) | 1);
	set_cloexec_of_fd(rfd, (flags & LINUX_O_CLOEXEC) != 0);
	set_cloexec_of_fd(wfd, (flags & LINUX_O_CLOEXEC) != 0);
	pair[0] = rfd;
	pair[1] = wfd;
	if (g_user->copy_to_user(fds, pair, sizeof(pair)) != 0)
	{
		close_fd(rfd);
		close_fd(wfd);
		return -EFAULT;
	}
	return 0;
}

int64_t early_rootfs_eventfd2(uint64_t initval, int flags)
{
	int efd;
	int fd;
	int idx;

	if (!g_ready)
		return -ENOSYS;
	if (flags & ~(EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE))
		return -EINVAL;
	if (initval > EFD_COUNT_MAX)
		return -EINVAL;
	for (efd = 0; efd < EFD_MAX; efd++)
	{
		if (!g_eventfd[efd].used)
			break;
	}
	if (efd >= EFD_MAX)
		return -EMFILE;
	fd = alloc_fd(IR0_EARLY_ROOTFS_EVENTFD);
	if (fd < 0)
		return fd;
	idx = slot_from_fd(fd);
	if (idx < 0)
	{
		close_fd(fd);
		return -EBADF;
	}
	g_eventfd[efd].used = 1;
	g_eventfd[efd].count = initval;
	g_eventfd[efd].nonblock = (flags & EFD_NONBLOCK) != 0;
	g_eventfd[efd].semaphore = (flags & EFD_SEMAPHORE) != 0;
	g_slots[idx].pipe_id = efd;
	set_flags_of_fd(fd, IR0_O_RDWR |
			    ((flags & EFD_NONBLOCK) ? LINUX_O_NONBLOCK : 0));
	set_cloexec_of_fd(fd, (flags & EFD_CLOEXEC) != 0);
	return fd;
}

int64_t early_rootfs_eventfd_write(int fd, uint64_t buffer, uint64_t count)
{
	int efd = pipe_id_of_fd(fd);
	uint64_t add = 0;

	if (efd < 0 || efd >= EFD_MAX || !g_eventfd[efd].used)
		return -EBADF;
	if (count < 8)
		return -EINVAL;
	if (!g_user || !g_user->copy_from_user ||
	    g_user->copy_from_user(&add, buffer, 8) != 0)
		return -EFAULT;
	if (add == 0xffffffffffffffffULL)
		return -EINVAL;
	if (g_eventfd[efd].count + add < g_eventfd[efd].count ||
	    g_eventfd[efd].count + add > EFD_COUNT_MAX)
		return wait_or_eagain(fd);
	g_eventfd[efd].count += add;
	return 8;
}

int64_t early_rootfs_memfd_create(uint64_t name, unsigned flags)
{
	char discard[32];
	int mid;
	int fd;
	int idx;

	if (!g_ready)
		return -ENOSYS;
	if (!ir0_early_memfd_flags_ok(flags))
		return -EINVAL;
	if (!name || !copy_user_cstr(name, discard, sizeof(discard)))
		return -EFAULT;
	for (mid = 0; mid < MEMFD_MAX; mid++)
	{
		if (!g_memfd[mid].used)
			break;
	}
	if (mid >= MEMFD_MAX)
		return -EMFILE;
	fd = alloc_fd(IR0_EARLY_ROOTFS_MEMFD);
	if (fd < 0)
		return fd;
	idx = slot_from_fd(fd);
	if (idx < 0)
	{
		close_fd(fd);
		return -EBADF;
	}
	g_memfd[mid].used = 1;
	g_memfd[mid].refs = 1;
	g_memfd[mid].len = 0;
	g_memfd[mid].mode = IR0_EARLY_ROOTFS_MODE_REG;
	g_slots[idx].pipe_id = mid;
	set_flags_of_fd(fd, IR0_O_RDWR);
	set_cloexec_of_fd(fd, (flags & IR0_EARLY_MFD_CLOEXEC) != 0);
	return fd;
}

int64_t early_rootfs_memfd_write(int fd, uint64_t buffer, uint64_t count)
{
	int mid = pipe_id_of_fd(fd);
	uint64_t *pos = pos_of_fd(fd);
	uint8_t tmp[256];
	uint64_t n;
	uint64_t i;
	uint64_t off;

	if (mid < 0 || mid >= MEMFD_MAX || !g_memfd[mid].used || !pos)
		return -EBADF;
	off = *pos;
	if (off >= MEMFD_CAP)
		return -EINVAL;
	n = count;
	if (n > sizeof(tmp))
		n = sizeof(tmp);
	if (off + n > MEMFD_CAP)
		n = MEMFD_CAP - off;
	if (n == 0)
		return -EINVAL;
	if (!g_user || !g_user->copy_from_user ||
	    g_user->copy_from_user(tmp, buffer, n) != 0)
		return -EFAULT;
	for (i = 0; i < n; i++)
		g_memfd[mid].data[off + i] = tmp[i];
	if (off + n > g_memfd[mid].len)
		g_memfd[mid].len = (uint32_t)(off + n);
	*pos = off + n;
	return (int64_t)n;
}

int64_t early_rootfs_timerfd_create(int clockid, int flags)
{
	int tid;
	int fd;
	int idx;

	if (!g_ready)
		return -ENOSYS;
	if (!ir0_early_timerfd_clock_ok((uint64_t)clockid) ||
	    !ir0_early_timerfd_create_flags_ok(flags))
		return -EINVAL;
	for (tid = 0; tid < TIMERFD_MAX; tid++)
	{
		if (!g_timerfd[tid].used)
			break;
	}
	if (tid >= TIMERFD_MAX)
		return -EMFILE;
	fd = alloc_fd(IR0_EARLY_ROOTFS_TIMERFD);
	if (fd < 0)
		return fd;
	idx = slot_from_fd(fd);
	if (idx < 0)
	{
		close_fd(fd);
		return -EBADF;
	}
	g_timerfd[tid].used = 1;
	g_timerfd[tid].refs = 1;
	g_timerfd[tid].clock = clockid;
	g_timerfd[tid].armed = 0;
	g_timerfd[tid].expire = 0;
	g_timerfd[tid].interval = 0;
	g_timerfd[tid].ticks = 0;
	g_slots[idx].pipe_id = tid;
	set_flags_of_fd(fd, IR0_O_RDWR |
			    ((flags & IR0_EARLY_TFD_NONBLOCK) ? LINUX_O_NONBLOCK :
							       0));
	set_cloexec_of_fd(fd, (flags & IR0_EARLY_TFD_CLOEXEC) != 0);
	return fd;
}

static int copy_itimerspec(uint64_t src, int64_t *fields)
{
	if (!fields || !g_user || !g_user->copy_from_user)
		return -EFAULT;
	return g_user->copy_from_user(fields, src, 32) == 0 ? 0 : -EFAULT;
}

int64_t early_rootfs_timerfd_settime(int fd, int flags, uint64_t new_value,
				     uint64_t old_value)
{
	int tid = pipe_id_of_fd(fd);
	int64_t spec[4];
	int64_t old[4];
	uint64_t now;
	uint64_t value_ticks;
	uint64_t interval_ticks;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_TIMERFD || tid < 0 ||
	    tid >= TIMERFD_MAX || !g_timerfd[tid].used)
		return -EBADF;
	if (flags & ~IR0_EARLY_TFD_TIMER_ABSTIME)
		return -EINVAL;
	if (!new_value)
		return -EFAULT;
	timerfd_advance_one(&g_timerfd[tid]);
	if (old_value)
	{
		uint64_t remain = 0;

		if (g_timerfd[tid].armed && g_clock_hz)
		{
			now = clock_now_ticks(g_timerfd[tid].clock);
			remain = (g_timerfd[tid].expire > now) ?
					 g_timerfd[tid].expire - now :
					 0;
		}
		old[0] = g_clock_hz ? (int64_t)(g_timerfd[tid].interval / g_clock_hz) : 0;
		old[1] = g_clock_hz ?
				 (int64_t)((g_timerfd[tid].interval % g_clock_hz) *
					   NS_PER_SEC / g_clock_hz) :
				 0;
		old[2] = g_clock_hz ? (int64_t)(remain / g_clock_hz) : 0;
		old[3] = g_clock_hz ?
				 (int64_t)((remain % g_clock_hz) * NS_PER_SEC /
					   g_clock_hz) :
				 0;
		if (!g_user || !g_user->copy_to_user ||
		    g_user->copy_to_user(old_value, old, 32) != 0)
			return -EFAULT;
	}
	if (copy_itimerspec(new_value, spec) < 0)
		return -EFAULT;
	if (spec[0] < 0 || spec[1] < 0 || spec[1] >= (int64_t)NS_PER_SEC ||
	    spec[2] < 0 || spec[3] < 0 || spec[3] >= (int64_t)NS_PER_SEC)
		return -EINVAL;
	interval_ticks = ir0_early_timespec_to_ticks(spec[0], spec[1], g_clock_hz);
	value_ticks = ir0_early_timespec_to_ticks(spec[2], spec[3], g_clock_hz);
	g_timerfd[tid].ticks = 0;
	g_timerfd[tid].interval = interval_ticks;
	if (spec[2] == 0 && spec[3] == 0)
	{
		g_timerfd[tid].armed = 0;
		g_timerfd[tid].expire = 0;
		return 0;
	}
	now = clock_now_ticks(g_timerfd[tid].clock);
	if (flags & IR0_EARLY_TFD_TIMER_ABSTIME)
		g_timerfd[tid].expire = value_ticks;
	else
		g_timerfd[tid].expire = now + (value_ticks ? value_ticks : 1);
	g_timerfd[tid].armed = 1;
	timerfd_advance_one(&g_timerfd[tid]);
	return 0;
}

int64_t early_rootfs_timerfd_gettime(int fd, uint64_t curr)
{
	int tid = pipe_id_of_fd(fd);
	int64_t out[4];
	uint64_t remain = 0;
	uint64_t now;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_TIMERFD || tid < 0 ||
	    tid >= TIMERFD_MAX || !g_timerfd[tid].used)
		return -EBADF;
	if (!curr)
		return -EFAULT;
	timerfd_advance_one(&g_timerfd[tid]);
	if (g_timerfd[tid].armed && g_clock_hz)
	{
		now = clock_now_ticks(g_timerfd[tid].clock);
		remain = (g_timerfd[tid].expire > now) ?
				 g_timerfd[tid].expire - now :
				 0;
	}
	out[0] = g_clock_hz ? (int64_t)(g_timerfd[tid].interval / g_clock_hz) : 0;
	out[1] = g_clock_hz ?
			 (int64_t)((g_timerfd[tid].interval % g_clock_hz) *
				   NS_PER_SEC / g_clock_hz) :
			 0;
	out[2] = g_clock_hz ? (int64_t)(remain / g_clock_hz) : 0;
	out[3] = g_clock_hz ?
			 (int64_t)((remain % g_clock_hz) * NS_PER_SEC / g_clock_hz) :
			 0;
	if (!g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(curr, out, 32) != 0)
		return -EFAULT;
	return 0;
}

int64_t early_rootfs_epoll_create1(int flags)
{
	int eid;
	int fd;
	int idx;
	unsigned s;

	if (!g_ready)
		return -ENOSYS;
	if (!ir0_early_epoll_create_flags_ok(flags))
		return -EINVAL;
	for (eid = 0; eid < EPOLL_MAX; eid++)
	{
		if (!g_epoll[eid].used)
			break;
	}
	if (eid >= EPOLL_MAX)
		return -EMFILE;
	fd = alloc_fd(IR0_EARLY_ROOTFS_EPOLL);
	if (fd < 0)
		return fd;
	idx = slot_from_fd(fd);
	if (idx < 0)
	{
		close_fd(fd);
		return -EBADF;
	}
	g_epoll[eid].used = 1;
	g_epoll[eid].refs = 1;
	for (s = 0; s < EPOLL_SLOTS; s++)
		g_epoll[eid].slot[s].used = 0;
	g_slots[idx].pipe_id = eid;
	set_flags_of_fd(fd, IR0_O_RDWR);
	set_cloexec_of_fd(fd, (flags & IR0_EARLY_EPOLL_CLOEXEC) != 0);
	return fd;
}

int64_t early_rootfs_epoll_ctl(int epfd, int op, int fd, uint64_t event)
{
	int eid = pipe_id_of_fd(epfd);
	unsigned s;
	int found = -1;
	int empty = -1;
	uint32_t ev;
	uint64_t data;

	if (node_of_fd(epfd) != IR0_EARLY_ROOTFS_EPOLL || eid < 0 ||
	    eid >= EPOLL_MAX || !g_epoll[eid].used)
		return -EBADF;
	if (!fd_is_open(fd))
		return -EBADF;
	if (node_of_fd(fd) == IR0_EARLY_ROOTFS_EPOLL)
		return -EINVAL;
	if (op != IR0_EARLY_EPOLL_CTL_ADD && op != IR0_EARLY_EPOLL_CTL_DEL &&
	    op != IR0_EARLY_EPOLL_CTL_MOD)
		return -EINVAL;
	for (s = 0; s < EPOLL_SLOTS; s++)
	{
		if (g_epoll[eid].slot[s].used && g_epoll[eid].slot[s].fd == fd)
			found = (int)s;
		if (!g_epoll[eid].slot[s].used && empty < 0)
			empty = (int)s;
	}
	if (op == IR0_EARLY_EPOLL_CTL_DEL)
	{
		if (found < 0)
			return -ENOENT;
		g_epoll[eid].slot[found].used = 0;
		return 0;
	}
	if (op == IR0_EARLY_EPOLL_CTL_ADD && found >= 0)
		return -EEXIST;
	if (op == IR0_EARLY_EPOLL_CTL_MOD && found < 0)
		return -ENOENT;
	if (op == IR0_EARLY_EPOLL_CTL_ADD && empty < 0)
		return -ENOSPC;
	if (!event || !g_user || !g_user->copy_from_user)
		return -EFAULT;
	if (g_user->copy_from_user(&ev, event, 4) != 0 ||
	    g_user->copy_from_user(&data, event + 8, 8) != 0)
		return -EFAULT;
	s = (unsigned)(op == IR0_EARLY_EPOLL_CTL_ADD ? empty : found);
	g_epoll[eid].slot[s].used = 1;
	g_epoll[eid].slot[s].fd = fd;
	g_epoll[eid].slot[s].events = ev;
	g_epoll[eid].slot[s].data = data;
	return 0;
}

int64_t early_rootfs_epoll_pwait(int epfd, uint64_t events, int maxevents,
				 int timeout)
{
	int eid = pipe_id_of_fd(epfd);
	unsigned s;
	int ready = 0;

	(void)timeout;
	if (node_of_fd(epfd) != IR0_EARLY_ROOTFS_EPOLL || eid < 0 ||
	    eid >= EPOLL_MAX || !g_epoll[eid].used)
		return -EBADF;
	if (maxevents < 1)
		return -EINVAL;
	if (!events)
		return -EFAULT;
	timerfd_advance_all();
	for (s = 0; s < EPOLL_SLOTS && ready < maxevents; s++)
	{
		int revents;
		uint32_t out_ev;
		uint32_t pad = 0;
		uint64_t data;
		uint64_t dest;

		if (!g_epoll[eid].slot[s].used)
			continue;
		if (node_of_fd(g_epoll[eid].slot[s].fd) == IR0_EARLY_ROOTFS_EPOLL)
			continue;
		revents = early_rootfs_poll_revents(g_epoll[eid].slot[s].fd,
						    (int)g_epoll[eid].slot[s].events);
		if (!revents)
			continue;
		out_ev = (uint32_t)revents;
		data = g_epoll[eid].slot[s].data;
		dest = events + (uint64_t)ready * IR0_EARLY_EPOLL_EVENT_BYTES;
		if (!g_user || !g_user->copy_to_user ||
		    g_user->copy_to_user(dest, &out_ev, 4) != 0 ||
		    g_user->copy_to_user(dest + 4, &pad, 4) != 0 ||
		    g_user->copy_to_user(dest + 8, &data, 8) != 0)
			return -EFAULT;
		ready++;
	}
	return ready;
}
