/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel/syscalls/erfs_util.c
 * Description: Early rootfs shared helpers — fd accessors, path walk, clocks.
 */

#include "erfs_internal.h"

int copy_user_cstr(uint64_t upath, char *buf, unsigned bufsz)
{
	unsigned i;

	if (!buf || bufsz == 0 || !g_user || !g_user->copy_from_user)
		return 0;
	for (i = 0; i < bufsz - 1; i++)
	{
		if (g_user->copy_from_user(&buf[i], upath + i, 1) != 0)
			return 0;
		if (buf[i] == '\0')
			return 1;
	}
	return 0;
}

int strings_equal(const char *left, const char *right)
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

int slot_from_fd(int fd)
{
	unsigned idx;

	if (fd < ROOTFS_FD_BASE)
		return -1;
	idx = (unsigned)(fd - ROOTFS_FD_BASE);
	if (idx >= ROOTFS_MAX_OPEN || !g_slots[idx].used)
		return -1;
	return (int)idx;
}

int fd_is_open(int fd)
{
	if (fd >= 0 && fd <= 2)
		return g_stdio_open[fd];
	return slot_from_fd(fd) >= 0;
}

enum ir0_early_rootfs_node node_of_fd(int fd)
{
	int idx;

	if (fd >= 0 && fd <= 2)
		return g_stdio_open[fd] ? g_stdio_node[fd] : IR0_EARLY_ROOTFS_NONE;
	idx = slot_from_fd(fd);
	if (idx < 0)
		return IR0_EARLY_ROOTFS_NONE;
	return g_slots[idx].node;
}

uint64_t *pos_of_fd(int fd)
{
	int idx;
	struct vfs_file *f;

	if (g_fd_ops && g_fd_ops->file)
	{
		f = g_fd_ops->file(fd);
		if (f)
			return (uint64_t *)&f->pos;
	}
	if (fd >= 0 && fd <= 2)
		return g_stdio_open[fd] ? &g_stdio_pos[fd] : 0;
	idx = slot_from_fd(fd);
	if (idx < 0)
		return 0;
	return &g_slots[idx].pos;
}

int flags_of_fd(int fd)
{
	int idx;

	if (fd >= 0 && fd <= 2)
		return g_stdio_open[fd] ? g_stdio_flags[fd] : 0;
	idx = slot_from_fd(fd);
	if (idx < 0)
		return 0;
	return g_slots[idx].flags;
}

void set_flags_of_fd(int fd, int flags)
{
	int idx;

	if (fd >= 0 && fd <= 2)
	{
		g_stdio_flags[fd] = flags;
		return;
	}
	idx = slot_from_fd(fd);
	if (idx >= 0)
		g_slots[idx].flags = flags;
}

void set_cloexec_of_fd(int fd, int cloexec)
{
	int idx;

	if (fd >= 0 && fd <= 2)
	{
		g_stdio_cloexec[fd] = cloexec ? 1 : 0;
		return;
	}
	idx = slot_from_fd(fd);
	if (idx >= 0)
		g_slots[idx].cloexec = cloexec ? 1 : 0;
}

int status_flags(int flags)
{
	return flags & (LINUX_O_ACCMODE | LINUX_O_APPEND | LINUX_O_NONBLOCK);
}

int vda_present(void)
{
	return ir0_block_name_is_present("vda");
}

uint64_t vda_bytes(void)
{
	return ir0_block_sector_count_by_name("vda") * 512ULL;
}

int pipe_id_of_fd(int fd)
{
	int idx;

	if (fd >= 0 && fd <= 2)
		return -1;
	idx = slot_from_fd(fd);
	if (idx < 0)
		return -1;
	return g_slots[idx].pipe_id;
}

int64_t wait_or_eagain(int fd)
{
	if (fd >= 0 && (flags_of_fd(fd) & LINUX_O_NONBLOCK))
		return -EAGAIN;
	(void)early_process_request_yield();
	return -EAGAIN;
}

struct early_pipe *pipe_of_fd(int fd)
{
	enum ir0_early_rootfs_node node = node_of_fd(fd);
	int id;

	if (node == IR0_EARLY_ROOTFS_PIPE_R || node == IR0_EARLY_ROOTFS_PIPE_W)
	{
		id = pipe_id_of_fd(fd);
		if (id < 0 || id >= PIPE_MAX || !g_pipes[id].used)
			return 0;
		return &g_pipes[id];
	}
	if (node == IR0_EARLY_ROOTFS_FIFO)
	{
		int fsid = pipe_id_of_fd(fd);
		int fifo;

		if (fsid < 0 || fsid >= RAMTAB_MAX || !ramtab_table()[fsid].used)
			return 0;
		fifo = ramtab_table()[fsid].fifo_pipe;
		if (fifo < 0 || fifo >= PIPE_MAX || !g_pipes[fifo].used)
			return 0;
		return &g_pipes[fifo];
	}
	return 0;
}

int64_t splice_wait(int fd, unsigned flags)
{
	if (flags & IR0_EARLY_SPLICE_F_NONBLOCK)
		return -EAGAIN;
	return wait_or_eagain(fd);
}

int load_loff(uint64_t uptr, int64_t *out, int64_t fallback)
{
	if (!out)
		return -EINVAL;
	if (!uptr)
	{
		*out = fallback;
		return 0;
	}
	if (!g_user || !g_user->copy_from_user ||
	    g_user->copy_from_user(out, uptr, sizeof(*out)) != 0)
		return -EFAULT;
	if (*out < 0)
		return -EINVAL;
	return 0;
}

int store_loff(uint64_t uptr, int64_t value)
{
	if (!uptr)
		return 0;
	if (!g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(uptr, &value, sizeof(value)) != 0)
		return -EFAULT;
	return 0;
}

int unix_id_of_fd(int fd)
{
	int encoded = pipe_id_of_fd(fd);

	if (encoded >= UNIX_ID_BASE && encoded < UNIX_ID_BASE + UNIX_MAX)
		return encoded - UNIX_ID_BASE;
	return -1;
}

int sock_pair_of_fd(int fd)
{
	int encoded = pipe_id_of_fd(fd);

	if (encoded >= 0 && encoded < SOCKPAIR_MAX * 2)
		return encoded >> 1;
	return -1;
}

uint64_t clock_now_ticks(int clock)
{
	uint64_t now = g_clock_mono;

	if (clock == IR0_EARLY_CLOCK_REALTIME && g_clock_hz)
		now += (uint64_t)g_clock_realtime_off * g_clock_hz;
	return now;
}

void timerfd_advance_one(struct early_timerfd *t)
{
	uint64_t now;

	if (!t || !t->used || !t->armed || !g_clock_hz)
		return;
	now = clock_now_ticks(t->clock);
	while (t->armed && now >= t->expire)
	{
		t->ticks++;
		if (t->interval == 0)
		{
			t->armed = 0;
			break;
		}
		t->expire += t->interval;
	}
}

void timerfd_advance_all(void)
{
	unsigned i;

	for (i = 0; i < TIMERFD_MAX; i++)
		timerfd_advance_one(&g_timerfd[i]);
}

void early_rootfs_note_clock(uint64_t mono_ticks, uint64_t hz,
			     int64_t realtime_off)
{
	g_clock_mono = mono_ticks;
	g_clock_hz = hz;
	g_clock_realtime_off = realtime_off;
	timerfd_advance_all();
}
int early_rootfs_ready(void)
{
	return g_ready;
}

int node_is_dir(enum ir0_early_rootfs_node node)
{
	return node == IR0_EARLY_ROOTFS_ROOT ||
	       node == IR0_EARLY_ROOTFS_DEV ||
	       node == IR0_EARLY_ROOTFS_TMP ||
	       node == IR0_EARLY_ROOTFS_USERDIR;
}

void apply_chroot(char *pbuf, unsigned cap)
{
	const char *prefix;
	char joined[32];
	unsigned i;
	unsigned j;

	if (!pbuf || cap == 0 || g_chroot == IR0_EARLY_ROOTFS_ROOT)
		return;
	if (pbuf[0] != '/')
		return;
	if (g_chroot == IR0_EARLY_ROOTFS_DEV)
		prefix = "/dev";
	else if (g_chroot == IR0_EARLY_ROOTFS_TMP)
		prefix = "/tmp";
	else if (g_chroot == IR0_EARLY_ROOTFS_USERDIR)
		prefix = g_chroot_path;
	else
		return;
	if (pbuf[1] == '\0')
	{
		for (i = 0; prefix[i] != '\0' && i + 1 < cap; i++)
			pbuf[i] = prefix[i];
		pbuf[i] = '\0';
		return;
	}
	i = 0;
	while (prefix[i] != '\0' && i < sizeof(joined) - 1)
	{
		joined[i] = prefix[i];
		i++;
	}
	j = 0;
	while (pbuf[j] != '\0' && i < sizeof(joined) - 1)
	{
		joined[i] = pbuf[j];
		i++;
		j++;
	}
	joined[i] = '\0';
	for (i = 0; joined[i] != '\0' && i + 1 < cap; i++)
		pbuf[i] = joined[i];
	pbuf[i] = '\0';
}

enum ir0_early_rootfs_node lookup_user_path(char *pbuf)
{
	enum ir0_early_rootfs_node node;
	int idx;

	apply_chroot(pbuf, 32);
	node = ir0_early_rootfs_path_node(pbuf);
	if (node != IR0_EARLY_ROOTFS_NONE)
		return node;
	node = bpath_lookup(pbuf);
	if (node != IR0_EARLY_ROOTFS_NONE)
		return node;
	idx = ramtab_find(pbuf);
	if (idx < 0)
		return IR0_EARLY_ROOTFS_NONE;
	return ramtab_node_kind(ramtab_table()[idx].mode);
}

int follow_path(char *pbuf, unsigned cap)
{
	int hops;

	if (!pbuf || cap < 2)
		return 0;
	for (hops = 0; hops < 8; hops++)
	{
		int idx = ramtab_find(pbuf);
		char tgt[RAMTAB_NAME];
		char parent[RAMTAB_NAME];
		unsigned n;
		unsigned i;

		if (idx < 0)
			return 1;
		if ((ramtab_table()[idx].mode & S_IFMT) != S_IFLNK)
			return 1;
		n = ramtab_table()[idx].size;
		if (n == 0 || n >= sizeof(tgt))
			return 0;
		for (i = 0; i < n; i++)
			tgt[i] = (char)(ramtab_table()[idx].data ? ramtab_table()[idx].data[i] :
							 ramtab_table()[idx].own[i]);
		tgt[n] = '\0';
		if (tgt[0] == '/')
		{
			for (i = 0; tgt[i] != '\0' && i + 1 < cap; i++)
				pbuf[i] = tgt[i];
			pbuf[i] = '\0';
		}
		else
		{
			if (!ramtab_parent(ramtab_table()[idx].path, parent, sizeof(parent)))
				return 0;
			i = 0;
			while (parent[i] != '\0' && i + 1 < cap)
			{
				pbuf[i] = parent[i];
				i++;
			}
			if (i == 0 || pbuf[i - 1] != '/')
			{
				if (i + 1 >= cap)
					return 0;
				pbuf[i++] = '/';
			}
			n = 0;
			while (tgt[n] != '\0' && i + 1 < cap)
			{
				pbuf[i] = tgt[n];
				i++;
				n++;
			}
			pbuf[i] = '\0';
			if (tgt[n] != '\0')
				return 0;
		}
		apply_chroot(pbuf, cap);
	}
	return -1;
}
static int path_of_node(enum ir0_early_rootfs_node node, int id, char *out,
			unsigned cap)
{
	if (!out || !cap)
		return -EINVAL;
	switch (node)
	{
	case IR0_EARLY_ROOTFS_ROOT:
		copy_kstr(out, cap, "/");
		return 0;
	case IR0_EARLY_ROOTFS_INIT:
		copy_kstr(out, cap, "/init");
		return 0;
	case IR0_EARLY_ROOTFS_DEV:
		copy_kstr(out, cap, "/dev");
		return 0;
	case IR0_EARLY_ROOTFS_CONSOLE:
		copy_kstr(out, cap, "/dev/console");
		return 0;
	case IR0_EARLY_ROOTFS_NULL:
		copy_kstr(out, cap, "/dev/null");
		return 0;
	case IR0_EARLY_ROOTFS_ZERO:
		copy_kstr(out, cap, "/dev/zero");
		return 0;
	case IR0_EARLY_ROOTFS_TTY:
		copy_kstr(out, cap, "/dev/tty");
		return 0;
	case IR0_EARLY_ROOTFS_VDA:
		copy_kstr(out, cap, "/dev/vda");
		return 0;
	case IR0_EARLY_ROOTFS_TMP:
		copy_kstr(out, cap, "/tmp");
		return 0;
	case IR0_EARLY_ROOTFS_FILE:
	case IR0_EARLY_ROOTFS_USERDIR:
	case IR0_EARLY_ROOTFS_FIFO:
	case IR0_EARLY_ROOTFS_UNIX:
	case IR0_EARLY_ROOTFS_SYMLINK:
		if (id < 0 || id >= RAMTAB_MAX || !ramtab_table()[id].used)
			return -EBADFD;
		copy_kstr(out, cap, ramtab_table()[id].path);
		return 0;
	default:
		return -EBADFD;
	}
}

int path_of_fd(int fd, char *out, unsigned cap)
{
	return path_of_node(node_of_fd(fd), pipe_id_of_fd(fd), out, cap);
}

int parent_exists(const char *path)
{
	char parent[RAMTAB_NAME];

	if (!ramtab_parent(path, parent, sizeof(parent)))
		return 0;
	if (ir0_early_rootfs_path_node(parent) != IR0_EARLY_ROOTFS_NONE)
		return 1;
	return ramtab_find(parent) >= 0;
}

int join_under_cwd(char *pbuf, unsigned cap)
{
	char joined[RAMTAB_NAME];
	const char *cwd = g_cwd_path[0] ? g_cwd_path : "/";
	unsigned i = 0;
	unsigned j = 0;

	if (!pbuf || cap < 2 || pbuf[0] == '/')
		return 1;
	while (cwd[i] != '\0' && i < sizeof(joined) - 2)
	{
		joined[i] = cwd[i];
		i++;
	}
	if (i == 0 || joined[i - 1] != '/')
		joined[i++] = '/';
	while (pbuf[j] != '\0' && i + 1 < sizeof(joined))
	{
		joined[i] = pbuf[j];
		i++;
		j++;
	}
	if (pbuf[j] != '\0' || i + 1 > cap)
		return 0;
	joined[i] = '\0';
	for (i = 0; joined[i] != '\0' && i + 1 < cap; i++)
		pbuf[i] = joined[i];
	pbuf[i] = '\0';
	return 1;
}

int fd_espipe(int fd)
{
	enum ir0_early_rootfs_node node = node_of_fd(fd);

	if (node == IR0_EARLY_ROOTFS_NONE)
		return -EBADF;
	if (node == IR0_EARLY_ROOTFS_CONSOLE || node == IR0_EARLY_ROOTFS_TTY ||
	    node == IR0_EARLY_ROOTFS_PIPE_R || node == IR0_EARLY_ROOTFS_PIPE_W ||
	    node == IR0_EARLY_ROOTFS_EVENTFD || node == IR0_EARLY_ROOTFS_TIMERFD ||
	    node == IR0_EARLY_ROOTFS_EPOLL || node == IR0_EARLY_ROOTFS_SOCK ||
	    node == IR0_EARLY_ROOTFS_FIFO || node == IR0_EARLY_ROOTFS_PIDFD)
		return -ESPIPE;
	return 0;
}
