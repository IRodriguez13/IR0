/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: early_rootfs.c
 * Description: Architecture-neutral staged root, /init and /dev/console table
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include <ir0/blockdev.h>
#include <ir0/early_cpio.h>
#include <ir0/early_rootfs.h>
#include <ir0/fcntl.h>
#include <ir0/statfs.h>

#define EBADF  9
#define EACCES 13
#define EFAULT 14
#define EINVAL 22
#define EISDIR 21
#define ENOENT 2
#define ENOTDIR 20
#define EMFILE 24
#define ESPIPE 29
#define ERANGE 34
#define E2BIG  7
#define EAGAIN 11
#define EPIPE  32
#define EIO    5
#define ENOSYS 38
#define EPERM  1
#define EEXIST 17
#define EBUSY  16
#define EROFS  30
#define ENOTEMPTY 39
#define ENOEXEC 8
#define ENOTTY 25
#define AT_REMOVEDIR 0x200
#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_NB 4
#define LOCK_UN 8

#define ROOTFS_MAX_OPEN 16U
#define ROOTFS_FD_BASE  3
#define ROOTFS_FD_LIMIT ((int)(ROOTFS_FD_BASE + ROOTFS_MAX_OPEN))
#define LINUX_O_ACCMODE  IR0_O_ACCMODE

#define EXEC_ARGC_MAX 8
#define EXEC_ARG_LEN  32

struct rootfs_slot
{
	int used;
	enum ir0_early_rootfs_node node;
	uint64_t pos;
	int flags;
	int cloexec;
	int pipe_id;
};

#define PIPE_MAX 2
#define PIPE_BUF 1024
#define EFD_MAX 2
#define EFD_CLOEXEC 02000000
#define EFD_NONBLOCK 00004000
#define EFD_SEMAPHORE 1
#define EFD_COUNT_MAX 0xfffffffffffffffeULL
#define MEMFD_MAX 2
#define MEMFD_CAP 4096
#define TIMERFD_MAX 2
#define EPOLL_MAX 2
#define EPOLL_SLOTS 8
#define SOCKPAIR_MAX 4
#define UNIX_MAX 4
#define UNIX_ID_BASE 32
#define FS_FILE_CAP 512
#define EAFNOSUPPORT 97
#define ESOCKTNOSUPPORT 94
#define EPROTONOSUPPORT 93
#define ENOSPC 28
#define EMSGSIZE 90
#define ENAMETOOLONG 36
#define ENOTSOCK 88
#define EADDRINUSE 98
#define ECONNREFUSED 111
#define EISCONN 106
#define ENOTCONN 107
#define ENXIO 6
#define EOPNOTSUPP 95
#define EFBIG 27
#define NS_PER_SEC 1000000000ULL

struct early_pipe
{
	int used;
	unsigned rpos;
	unsigned len;
	int readers;
	int writers;
	uint8_t buf[PIPE_BUF];
};

static struct rootfs_slot g_slots[ROOTFS_MAX_OPEN];
static const uint8_t *g_blob;
static uint64_t g_blob_len;
static const struct early_user_services *g_user;
static int g_ready;
static int g_stdio_open[3];
static enum ir0_early_rootfs_node g_stdio_node[3];
static uint64_t g_stdio_pos[3];
static int g_stdio_flags[3];
static int g_stdio_cloexec[3];
static enum ir0_early_rootfs_node g_cwd;
static enum ir0_early_rootfs_node g_chroot;
static uint64_t g_init_vis;
static int g_init_from_block;
static int g_exec_pending;
static char g_exec_args[EXEC_ARGC_MAX][EXEC_ARG_LEN];
static char *g_exec_argv[EXEC_ARGC_MAX + 1];
static struct early_pipe g_pipes[PIPE_MAX];
static struct early_eventfd
{
	int used;
	uint64_t count;
	int nonblock;
	int semaphore;
} g_eventfd[EFD_MAX];

static struct early_memfd
{
	int used;
	int refs;
	uint32_t len;
	uint8_t data[MEMFD_CAP];
} g_memfd[MEMFD_MAX];

static struct early_timerfd
{
	int used;
	int refs;
	int clock;
	int armed;
	uint64_t expire;
	uint64_t interval;
	uint64_t ticks;
} g_timerfd[TIMERFD_MAX];

static struct early_epoll
{
	int used;
	int refs;
	struct
	{
		int used;
		int fd;
		uint32_t events;
		uint64_t data;
	} slot[EPOLL_SLOTS];
} g_epoll[EPOLL_MAX];

static struct early_sockpair
{
	int used;
	int refs[2];
	int dgram;
	struct
	{
		unsigned rpos;
		unsigned len;
		uint8_t buf[PIPE_BUF];
	} dir[2];
} g_sock[SOCKPAIR_MAX];

static uint64_t g_clock_mono;
static uint64_t g_clock_hz;
static int64_t g_clock_realtime_off;

#define FS_MAX 24
#define FS_NAME 40

struct early_fs_ent
{
	int used;
	char path[FS_NAME];
	uint32_t mode;
	uint32_t size;
	const uint8_t *data;
	int fifo_pipe;
	int writable;
	uint8_t own[FS_FILE_CAP];
};

static struct early_fs_ent g_fs[FS_MAX];

static struct early_unix
{
	int used;
	int refs;
	int stream;
	int bound_fs;
	int listening;
	int pending_pair;
	int pending_end;
	int peer_fs;
	char bound_path[FS_NAME];
	unsigned rpos;
	unsigned len;
	uint8_t buf[PIPE_BUF];
} g_unix[UNIX_MAX];
static int g_cpio_ready;
static char g_cwd_path[FS_NAME];
static char g_chroot_path[FS_NAME];

static int copy_user_cstr(uint64_t upath, char *buf, unsigned bufsz)
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

static int strings_equal(const char *left, const char *right)
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

static void reset_stdio(void)
{
	g_stdio_open[0] = 1;
	g_stdio_open[1] = 1;
	g_stdio_open[2] = 1;
	g_stdio_node[0] = IR0_EARLY_ROOTFS_CONSOLE;
	g_stdio_node[1] = IR0_EARLY_ROOTFS_CONSOLE;
	g_stdio_node[2] = IR0_EARLY_ROOTFS_CONSOLE;
	g_stdio_pos[0] = 0;
	g_stdio_pos[1] = 0;
	g_stdio_pos[2] = 0;
	g_stdio_flags[0] = O_RDWR;
	g_stdio_flags[1] = O_RDWR;
	g_stdio_flags[2] = O_RDWR;
	g_stdio_cloexec[0] = 0;
	g_stdio_cloexec[1] = 0;
	g_stdio_cloexec[2] = 0;
	g_cwd = IR0_EARLY_ROOTFS_ROOT;
	g_chroot = IR0_EARLY_ROOTFS_ROOT;
}

static int slot_from_fd(int fd)
{
	unsigned idx;

	if (fd < ROOTFS_FD_BASE)
		return -1;
	idx = (unsigned)(fd - ROOTFS_FD_BASE);
	if (idx >= ROOTFS_MAX_OPEN || !g_slots[idx].used)
		return -1;
	return (int)idx;
}

static int fd_is_open(int fd)
{
	if (fd >= 0 && fd <= 2)
		return g_stdio_open[fd];
	return slot_from_fd(fd) >= 0;
}

static enum ir0_early_rootfs_node node_of_fd(int fd)
{
	int idx;

	if (fd >= 0 && fd <= 2)
		return g_stdio_open[fd] ? g_stdio_node[fd] : IR0_EARLY_ROOTFS_NONE;
	idx = slot_from_fd(fd);
	if (idx < 0)
		return IR0_EARLY_ROOTFS_NONE;
	return g_slots[idx].node;
}

static uint64_t *pos_of_fd(int fd)
{
	int idx;

	if (fd >= 0 && fd <= 2)
		return g_stdio_open[fd] ? &g_stdio_pos[fd] : 0;
	idx = slot_from_fd(fd);
	if (idx < 0)
		return 0;
	return &g_slots[idx].pos;
}

static int flags_of_fd(int fd)
{
	int idx;

	if (fd >= 0 && fd <= 2)
		return g_stdio_open[fd] ? g_stdio_flags[fd] : 0;
	idx = slot_from_fd(fd);
	if (idx < 0)
		return 0;
	return g_slots[idx].flags;
}

static void set_flags_of_fd(int fd, int flags)
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

static int cloexec_of_fd(int fd)
{
	int idx;

	if (fd >= 0 && fd <= 2)
		return g_stdio_open[fd] ? g_stdio_cloexec[fd] : 0;
	idx = slot_from_fd(fd);
	if (idx < 0)
		return 0;
	return g_slots[idx].cloexec;
}

static void set_cloexec_of_fd(int fd, int cloexec)
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

static int status_flags(int flags)
{
	return flags & (LINUX_O_ACCMODE | LINUX_O_APPEND | LINUX_O_NONBLOCK);
}

static int vda_present(void)
{
	return ir0_block_name_is_present("vda");
}

static uint64_t vda_bytes(void)
{
	return ir0_block_sector_count_by_name("vda") * 512ULL;
}

static int pipe_id_of_fd(int fd)
{
	int idx;

	if (fd >= 0 && fd <= 2)
		return -1;
	idx = slot_from_fd(fd);
	if (idx < 0)
		return -1;
	return g_slots[idx].pipe_id;
}

static int parent_exists(const char *path);
static int join_under_cwd(char *pbuf, unsigned cap);
static int unix_find_bound(const char *path, int stream);
static int64_t unix_dgram_deposit(int uid, const uint8_t *tmp, unsigned count);

static int unix_id_of_fd(int fd)
{
	int encoded = pipe_id_of_fd(fd);

	if (encoded >= UNIX_ID_BASE && encoded < UNIX_ID_BASE + UNIX_MAX)
		return encoded - UNIX_ID_BASE;
	return -1;
}

static int sock_pair_of_fd(int fd)
{
	int encoded = pipe_id_of_fd(fd);

	if (encoded >= 0 && encoded < SOCKPAIR_MAX * 2)
		return encoded >> 1;
	return -1;
}

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

static uint64_t clock_now_ticks(int clock)
{
	uint64_t now = g_clock_mono;

	if (clock == IR0_EARLY_CLOCK_REALTIME && g_clock_hz)
		now += (uint64_t)g_clock_realtime_off * g_clock_hz;
	return now;
}

static void timerfd_advance_one(struct early_timerfd *t)
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

static void timerfd_advance_all(void)
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

static int fs_path_eq(const char *left, const char *right)
{
	return strings_equal(left, right);
}

static int fs_parent(const char *path, char *parent, unsigned cap)
{
	unsigned n = 0;
	unsigned last = 0;
	unsigned i;

	if (!path || !parent || cap < 2 || path[0] != '/')
		return 0;
	while (path[n] != '\0')
	{
		if (path[n] == '/' && n > 0)
			last = n;
		n++;
	}
	if (n <= 1)
	{
		parent[0] = '/';
		parent[1] = '\0';
		return 1;
	}
	if (last == 0)
	{
		parent[0] = '/';
		parent[1] = '\0';
		return 1;
	}
	if (last + 1 >= cap)
		return 0;
	for (i = 0; i < last; i++)
		parent[i] = path[i];
	parent[last] = '\0';
	return 1;
}

static const char *fs_basename(const char *path)
{
	unsigned i = 0;
	unsigned last = 0;

	if (!path || path[0] == '\0')
		return path;
	while (path[i] != '\0')
	{
		if (path[i] == '/' && path[i + 1] != '\0')
			last = i + 1;
		i++;
	}
	return path + last;
}

static int fs_find(const char *path)
{
	unsigned i;

	if (!path)
		return -1;
	for (i = 0; i < FS_MAX; i++)
	{
		if (g_fs[i].used && fs_path_eq(g_fs[i].path, path))
			return (int)i;
	}
	return -1;
}

static int fs_add(const char *path, uint32_t mode, uint32_t size,
		  const uint8_t *data, int writable)
{
	int idx;
	unsigned i;

	if (!path || path[0] != '/')
		return -EINVAL;
	if (writable && size > FS_FILE_CAP)
		return -EFBIG;
	idx = fs_find(path);
	if (idx >= 0)
	{
		g_fs[idx].mode = mode;
		g_fs[idx].size = size;
		g_fs[idx].writable = 0;
		g_fs[idx].data = data;
		return idx;
	}
	for (i = 0; i < FS_MAX; i++)
	{
		if (g_fs[i].used)
			continue;
		g_fs[i].used = 1;
		for (idx = 0; path[idx] != '\0' && idx + 1 < FS_NAME; idx++)
			g_fs[i].path[idx] = path[idx];
		if (path[idx] != '\0')
		{
			g_fs[i].used = 0;
			return -ENAMETOOLONG;
		}
		g_fs[i].path[idx] = '\0';
		g_fs[i].mode = mode;
		g_fs[i].size = size;
		g_fs[i].fifo_pipe = -1;
		g_fs[i].writable = writable;
		if (writable)
		{
			unsigned n;

			g_fs[i].data = g_fs[i].own;
			for (n = 0; n < FS_FILE_CAP; n++)
				g_fs[i].own[n] = 0;
			if (data && size)
			{
				for (n = 0; n < size; n++)
					g_fs[i].own[n] = data[n];
			}
		}
		else
			g_fs[i].data = data;
		return (int)i;
	}
	return -ENOSPC;
}

static enum ir0_early_rootfs_node fs_node_kind(uint32_t mode)
{
	uint32_t type = mode & S_IFMT;

	if (type == S_IFDIR)
		return IR0_EARLY_ROOTFS_USERDIR;
	if (type == S_IFIFO)
		return IR0_EARLY_ROOTFS_FIFO;
	if (type == S_IFSOCK)
		return IR0_EARLY_ROOTFS_UNIX;
	return IR0_EARLY_ROOTFS_FILE;
}

static int fs_child_count(const char *dir)
{
	char parent[FS_NAME];
	unsigned i;
	int n = 0;

	for (i = 0; i < FS_MAX; i++)
	{
		if (!g_fs[i].used)
			continue;
		if (!fs_parent(g_fs[i].path, parent, sizeof(parent)))
			continue;
		if (fs_path_eq(parent, dir) && !fs_path_eq(g_fs[i].path, dir))
			n++;
	}
	return n;
}

static int fs_child_at(const char *dir, int which, struct ir0_early_dirent_spec *spec)
{
	char parent[FS_NAME];
	unsigned i;
	int n = 0;

	if (!spec)
		return 0;
	for (i = 0; i < FS_MAX; i++)
	{
		uint32_t type;

		if (!g_fs[i].used)
			continue;
		if (!fs_parent(g_fs[i].path, parent, sizeof(parent)))
			continue;
		if (!fs_path_eq(parent, dir) || fs_path_eq(g_fs[i].path, dir))
			continue;
		if (n == which)
		{
			type = g_fs[i].mode & S_IFMT;
			spec->name = fs_basename(g_fs[i].path);
			spec->ino = 16 + i;
			if (type == S_IFDIR)
				spec->type = IR0_EARLY_DT_DIR;
			else if (type == S_IFIFO)
				spec->type = IR0_EARLY_DT_FIFO;
			else if (type == S_IFSOCK)
				spec->type = IR0_EARLY_DT_SOCK;
			else
				spec->type = IR0_EARLY_DT_REG;
			return 1;
		}
		n++;
	}
	return 0;
}

static int close_fd(int fd)
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
		    pipe_id < FS_MAX && g_fs[pipe_id].used)
		{
			int fifo = g_fs[pipe_id].fifo_pipe;
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
					g_fs[pipe_id].fifo_pipe = -1;
				}
			}
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
	g_slots[idx].used = 0;
	g_slots[idx].node = IR0_EARLY_ROOTFS_NONE;
	g_slots[idx].pos = 0;
	g_slots[idx].flags = 0;
	g_slots[idx].cloexec = 0;
	g_slots[idx].pipe_id = -1;
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

static int alloc_fd(enum ir0_early_rootfs_node node)
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
			return (int)(ROOTFS_FD_BASE + i);
		}
	}
	return -EMFILE;
}

void early_rootfs_bind(const uint8_t *init_blob, uint64_t init_len,
		       const struct early_user_services *user)
{
	unsigned i;

	g_blob = init_blob;
	g_blob_len = init_len;
	g_init_vis = init_len;
	g_user = user;
	for (i = 0; i < ROOTFS_MAX_OPEN; i++)
	{
		g_slots[i].used = 0;
		g_slots[i].node = IR0_EARLY_ROOTFS_NONE;
		g_slots[i].pos = 0;
		g_slots[i].flags = 0;
		g_slots[i].cloexec = 0;
		g_slots[i].pipe_id = -1;
	}
	for (i = 0; i < PIPE_MAX; i++)
		g_pipes[i].used = 0;
	for (i = 0; i < EFD_MAX; i++)
		g_eventfd[i].used = 0;
	for (i = 0; i < MEMFD_MAX; i++)
		g_memfd[i].used = 0;
	for (i = 0; i < TIMERFD_MAX; i++)
		g_timerfd[i].used = 0;
	for (i = 0; i < EPOLL_MAX; i++)
		g_epoll[i].used = 0;
	for (i = 0; i < SOCKPAIR_MAX; i++)
		g_sock[i].used = 0;
	for (i = 0; i < UNIX_MAX; i++)
		g_unix[i].used = 0;
	g_clock_mono = 0;
	g_clock_hz = 0;
	g_clock_realtime_off = 0;
	for (i = 0; i < FS_MAX; i++)
	{
		g_fs[i].used = 0;
		g_fs[i].fifo_pipe = -1;
		g_fs[i].writable = 0;
		g_fs[i].data = 0;
		g_fs[i].size = 0;
	}
	g_cpio_ready = 0;
	g_cwd_path[0] = '/';
	g_cwd_path[1] = '\0';
	g_chroot_path[0] = '/';
	g_chroot_path[1] = '\0';
	reset_stdio();
	g_exec_pending = 0;
	g_init_from_block = 0;
	g_ready = (init_blob != 0 && init_len > 0 && user &&
		   user->copy_from_user && user->copy_to_user);
}

const uint8_t *early_rootfs_init_blob(uint64_t *length)
{
	if (length)
		*length = g_blob_len;
	return g_blob;
}

int early_rootfs_init_from_block(void)
{
	return g_init_from_block;
}

int early_rootfs_cpio_ready(void)
{
	return g_cpio_ready;
}

int early_rootfs_try_bind_block(const char *name, uint8_t *buffer,
				uint64_t buffer_len,
				const struct early_user_services *user)
{
	uint8_t sector[512];
	uint64_t phoff;
	uint16_t phnum;
	uint16_t phentsize;
	uint64_t header_bytes;
	uint64_t image_bytes;
	uint64_t sectors;
	uint64_t i;

	if (!name || !buffer || buffer_len < 512 || !user)
		return -EINVAL;
	if (!ir0_block_name_is_present(name))
		return -ENOENT;
	if (ir0_block_read_by_name(name, 0, 1, sector) != 0)
		return -EIO;
	if (ir0_early_cpio_magic(sector))
	{
		uint64_t bytes = vda_bytes();
		uint64_t load;
		uint64_t off = 0;
		const uint8_t *init_data = 0;
		uint64_t init_len = 0;
		int saw_file = 0;

		if (bytes > buffer_len)
			bytes = buffer_len;
		load = (bytes + 511UL) / 512UL;
		for (i = 0; i < load; i++)
		{
			if (ir0_block_read_by_name(name, i, 1,
						   buffer + i * 512UL) != 0)
				return -EIO;
		}
		while (1)
		{
			struct ir0_early_cpio_member mem;
			char norm[FS_NAME];
			int ret;

			ret = ir0_early_cpio_next(buffer, bytes, off, &mem);
			if (ret <= 0)
				break;
			off = mem.next;
			if (!ir0_early_cpio_normalize(mem.name, norm, sizeof(norm)))
				continue;
			saw_file = 1;
			if ((mem.mode & S_IFMT) == S_IFREG &&
			    ir0_early_rootfs_path_eq(norm, "/init") &&
			    ir0_early_elf64_image_bytes(mem.data, mem.filesize))
			{
				init_data = mem.data;
				init_len = mem.filesize;
			}
		}
		if (!init_data || !init_len)
			return -ENOEXEC;
		early_rootfs_bind(init_data, init_len, user);
		g_init_from_block = 1;
		off = 0;
		while (1)
		{
			struct ir0_early_cpio_member mem;
			char norm[FS_NAME];
			int ret;

			ret = ir0_early_cpio_next(buffer, bytes, off, &mem);
			if (ret <= 0)
				break;
			off = mem.next;
			if (!ir0_early_cpio_normalize(mem.name, norm, sizeof(norm)))
				continue;
			fs_add(norm, mem.mode, mem.filesize, mem.data, 0);
		}
		g_cpio_ready = saw_file;
		return 0;
	}
	if (sector[0] != 0x7f || sector[1] != 'E' || sector[2] != 'L' ||
	    sector[3] != 'F' || sector[4] != 2 || sector[5] != 1)
		return -ENOEXEC;
	for (i = 0; i < 512; i++)
		buffer[i] = sector[i];
	phoff = ir0_early_le64(sector + 32);
	phentsize = ir0_early_le16(sector + 54);
	phnum = ir0_early_le16(sector + 56);
	header_bytes = 64;
	if (phoff && phentsize && phnum)
		header_bytes = phoff + (uint64_t)phnum * phentsize;
	if (header_bytes > buffer_len)
		return -E2BIG;
	if (header_bytes > 512)
	{
		sectors = (header_bytes + 511UL) / 512UL;
		for (i = 1; i < sectors; i++)
		{
			if (ir0_block_read_by_name(name, i, 1,
						   buffer + i * 512UL) != 0)
				return -EIO;
		}
	}
	image_bytes = ir0_early_elf64_image_bytes(buffer, buffer_len);
	if (image_bytes == 0)
		return -ENOEXEC;
	if (image_bytes > buffer_len)
		return -E2BIG;
	sectors = (image_bytes + 511UL) / 512UL;
	for (i = (header_bytes + 511UL) / 512UL; i < sectors; i++)
	{
		if (ir0_block_read_by_name(name, i, 1, buffer + i * 512UL) != 0)
			return -EIO;
	}
	early_rootfs_bind(buffer, image_bytes, user);
	g_init_from_block = 1;
	return 0;
}

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

int early_rootfs_ready(void)
{
	return g_ready;
}

static int node_is_dir(enum ir0_early_rootfs_node node)
{
	return node == IR0_EARLY_ROOTFS_ROOT ||
	       node == IR0_EARLY_ROOTFS_DEV ||
	       node == IR0_EARLY_ROOTFS_TMP ||
	       node == IR0_EARLY_ROOTFS_USERDIR;
}

static void apply_chroot(char *pbuf, unsigned cap)
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

static enum ir0_early_rootfs_node lookup_user_path(char *pbuf)
{
	enum ir0_early_rootfs_node node;
	int idx;

	apply_chroot(pbuf, 32);
	node = ir0_early_rootfs_path_node(pbuf);
	if (node != IR0_EARLY_ROOTFS_NONE)
		return node;
	idx = fs_find(pbuf);
	if (idx < 0)
		return IR0_EARLY_ROOTFS_NONE;
	return fs_node_kind(g_fs[idx].mode);
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
	node = lookup_user_path(pbuf);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -ENOENT;
	if (node == IR0_EARLY_ROOTFS_VDA && !vda_present())
		return -ENOENT;
	if (node == IR0_EARLY_ROOTFS_UNIX)
		return -ENXIO;
	if ((flags & LINUX_O_DIRECTORY) && !node_is_dir(node))
		return -ENOTDIR;
	fd = alloc_fd(node);
	if (fd < 0)
		return fd;
	set_flags_of_fd(fd, status_flags(flags));
	set_cloexec_of_fd(fd, (flags & LINUX_O_CLOEXEC) != 0);
	if (node == IR0_EARLY_ROOTFS_FILE || node == IR0_EARLY_ROOTFS_FIFO ||
	    node == IR0_EARLY_ROOTFS_USERDIR)
	{
		int fsid = fs_find(pbuf);
		int sidx = slot_from_fd(fd);

		if (fsid >= 0 && sidx >= 0)
			g_slots[sidx].pipe_id = fsid;
		if (node == IR0_EARLY_ROOTFS_FIFO && fsid >= 0)
		{
			int pipe_id = g_fs[fsid].fifo_pipe;

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
				g_fs[fsid].fifo_pipe = pipe_id;
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
	if (count == 0)
		return 0;
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
			return p->writers ? -EAGAIN : 0;
		take = p->len;
		if (take > sizeof(tmp))
			take = (unsigned)sizeof(tmp);
		if ((uint64_t)take > count)
			take = (unsigned)count;
		for (i = 0; i < take; i++)
		{
			tmp[i] = p->buf[p->rpos];
			p->rpos = (p->rpos + 1U) % PIPE_BUF;
		}
		p->len -= take;
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
			return -EAGAIN;
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
				return -EAGAIN;
			if (g_unix[uid].len < 2)
				return -EIO;
			{
				uint16_t mlen;
				unsigned hdr = g_unix[uid].rpos;

				mlen = (uint16_t)g_unix[uid].buf[hdr];
				mlen |= (uint16_t)g_unix[uid].buf[(hdr + 1U) %
								  PIPE_BUF]
					<< 8;
				if (g_unix[uid].len < 2U + mlen)
					return -EIO;
				g_unix[uid].rpos = (hdr + 2U) % PIPE_BUF;
				g_unix[uid].len -= 2U;
				take = mlen;
				if (take > sizeof(tmp))
					take = (unsigned)sizeof(tmp);
				if ((uint64_t)take > count)
					take = (unsigned)count;
				for (i2 = 0; i2 < mlen; i2++)
				{
					uint8_t b = g_unix[uid].buf[g_unix[uid].rpos];

					g_unix[uid].rpos =
						(g_unix[uid].rpos + 1U) % PIPE_BUF;
					if (i2 < take)
						tmp[i2] = b;
				}
				g_unix[uid].len -= mlen;
				if (!g_user || !g_user->copy_to_user ||
				    g_user->copy_to_user(buffer, tmp, take) != 0)
					return -EFAULT;
				return (int64_t)take;
			}
		}
		if (pair < 0 || pair >= SOCKPAIR_MAX || !g_sock[pair].used)
			return -EBADF;
		src = 1 - end;
		if (g_sock[pair].dir[src].len == 0)
			return g_sock[pair].refs[src] ? -EAGAIN : 0;
		if (g_sock[pair].dgram)
		{
			uint16_t mlen;
			unsigned hdr = g_sock[pair].dir[src].rpos;

			if (g_sock[pair].dir[src].len < 2)
				return -EIO;
			mlen = (uint16_t)g_sock[pair].dir[src].buf[hdr];
			mlen |= (uint16_t)g_sock[pair].dir[src].buf[(hdr + 1U) %
								    PIPE_BUF]
				 << 8;
			if (g_sock[pair].dir[src].len < 2U + mlen)
				return -EIO;
			g_sock[pair].dir[src].rpos = (hdr + 2U) % PIPE_BUF;
			g_sock[pair].dir[src].len -= 2U;
			take = mlen;
			if (take > sizeof(tmp))
				take = (unsigned)sizeof(tmp);
			if ((uint64_t)take > count)
				take = (unsigned)count;
			for (i2 = 0; i2 < mlen; i2++)
			{
				uint8_t b = g_sock[pair].dir[src].buf[g_sock[pair].dir[src].rpos];

				g_sock[pair].dir[src].rpos =
					(g_sock[pair].dir[src].rpos + 1U) %
					PIPE_BUF;
				if (i2 < take)
					tmp[i2] = b;
			}
			g_sock[pair].dir[src].len -= mlen;
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
		for (i2 = 0; i2 < take; i2++)
		{
			tmp[i2] = g_sock[pair].dir[src].buf[g_sock[pair].dir[src].rpos];
			g_sock[pair].dir[src].rpos =
				(g_sock[pair].dir[src].rpos + 1U) % PIPE_BUF;
		}
		g_sock[pair].dir[src].len -= take;
		if (!g_user || !g_user->copy_to_user ||
		    g_user->copy_to_user(buffer, tmp, take) != 0)
			return -EFAULT;
		return (int64_t)take;
	}
	if (node == IR0_EARLY_ROOTFS_EPOLL)
		return -EINVAL;
	if (node == IR0_EARLY_ROOTFS_FILE)
	{
		int fsid = pipe_id_of_fd(fd);
		uint64_t *pos = pos_of_fd(fd);
		uint64_t remain;

		if (fsid < 0 || fsid >= FS_MAX || !g_fs[fsid].used || !pos)
			return -EBADF;
		if (*pos >= g_fs[fsid].size)
			return 0;
		remain = g_fs[fsid].size - *pos;
		n = count > remain ? remain : count;
		if (n > sizeof(tmp))
			n = sizeof(tmp);
		for (i = 0; i < n; i++)
			tmp[i] = g_fs[fsid].data ? g_fs[fsid].data[*pos + i] : 0;
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
		if (fsid < 0 || fsid >= FS_MAX || !g_fs[fsid].used)
			return -EBADF;
		pipe_id = g_fs[fsid].fifo_pipe;
		if (pipe_id < 0 || pipe_id >= PIPE_MAX)
			return -EBADF;
		p = &g_pipes[pipe_id];
		if (!p->used)
			return -EBADF;
		if (p->len == 0)
			return p->writers ? -EAGAIN : 0;
		take = p->len;
		if (take > sizeof(tmp))
			take = (unsigned)sizeof(tmp);
		if ((uint64_t)take > count)
			take = (unsigned)count;
		for (i = 0; i < take; i++)
		{
			tmp[i] = p->buf[p->rpos];
			p->rpos = (p->rpos + 1U) % PIPE_BUF;
		}
		p->len -= take;
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

int64_t early_rootfs_close(int fd)
{
	return close_fd(fd);
}

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
	if (!strings_equal(pbuf, proc_self_exe))
	{
		/*
		 * Linux readlinkat(2): ENOENT if the path is missing,
		 * EINVAL if the named file exists and is not a symlink.
		 */
		if (lookup_user_path(pbuf) != IR0_EARLY_ROOTFS_NONE)
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
	return 0;
}

int64_t early_rootfs_newfstatat(int dirfd, uint64_t path, uint64_t statbuf,
				int flags)
{
	char pbuf[32];
	enum ir0_early_rootfs_node node;
	struct ir0_early_stat64 st;

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
		uint64_t size = g_blob_len;
		int fsid;

		if (node == IR0_EARLY_ROOTFS_VDA)
			size = vda_bytes();
		else if (node == IR0_EARLY_ROOTFS_INIT)
			size = g_init_vis;
		else if (node == IR0_EARLY_ROOTFS_FILE)
		{
			fsid = fs_find(pbuf);
			size = (fsid >= 0 && g_fs[fsid].used) ? g_fs[fsid].size :
								0;
		}
		else if (node == IR0_EARLY_ROOTFS_UNIX ||
			 node == IR0_EARLY_ROOTFS_FIFO ||
			 node == IR0_EARLY_ROOTFS_USERDIR)
			size = 0;
		ir0_early_rootfs_fill_stat(node, size, &st);
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
		mid = pipe_id_of_fd(fd);
		size = (mid >= 0 && mid < FS_MAX && g_fs[mid].used) ?
			       g_fs[mid].size :
			       0;
	}
	else
		size = g_blob_len;
	ir0_early_rootfs_fill_stat(node, size, &st);
	if (!g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(statbuf, &st, sizeof(st)) != 0)
		return -EFAULT;
	return 0;
}

int64_t early_rootfs_chdir(uint64_t path)
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
	g_cwd = node;
	{
		unsigned i;

		for (i = 0; pbuf[i] != '\0' && i + 1 < sizeof(g_cwd_path); i++)
			g_cwd_path[i] = pbuf[i];
		g_cwd_path[i] = '\0';
	}
	return 0;
}

int64_t early_rootfs_getdents64(int fd, uint64_t buffer, uint64_t count)
{
	enum ir0_early_rootfs_node node;
	int idx;
	unsigned total;
	unsigned written = 0;
	uint8_t rec[32];
	uint8_t tmp[256];
	unsigned tmp_used = 0;

	node = node_of_fd(fd);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -EBADF;
	if (!node_is_dir(node))
		return -ENOTDIR;
	{
		const char *dirpath = "/";
		unsigned base;
		int fsid = pipe_id_of_fd(fd);

		if (node == IR0_EARLY_ROOTFS_DEV)
			dirpath = "/dev";
		else if (node == IR0_EARLY_ROOTFS_TMP)
			dirpath = "/tmp";
		else if (node == IR0_EARLY_ROOTFS_USERDIR && fsid >= 0 &&
			 fsid < FS_MAX && g_fs[fsid].used)
			dirpath = g_fs[fsid].path;
		base = ir0_early_rootfs_dir_count(node);
		if (node == IR0_EARLY_ROOTFS_USERDIR && base == 0)
			base = 2;
		if (node == IR0_EARLY_ROOTFS_DEV && vda_present())
			base++;
		total = base + (unsigned)fs_child_count(dirpath);
		idx = slot_from_fd(fd);
		if (idx < 0)
			return -EBADF;
		if (count == 0)
			return -EINVAL;
		while (g_slots[idx].pos < total)
		{
			struct ir0_early_dirent_spec spec;
			unsigned reclen;
			unsigned i;
			unsigned syn = ir0_early_rootfs_dir_count(node);

			if (node == IR0_EARLY_ROOTFS_USERDIR && syn == 0)
				syn = 2;
			if (g_slots[idx].pos < syn)
			{
				if (node == IR0_EARLY_ROOTFS_USERDIR &&
				    ir0_early_rootfs_dir_count(node) == 0)
				{
					spec.name = (g_slots[idx].pos == 0) ? "." : "..";
					spec.ino = (g_slots[idx].pos == 0) ? 18 : 1;
					spec.type = IR0_EARLY_DT_DIR;
				}
				else
					spec = ir0_early_rootfs_dir_entry(node,
							 (unsigned)g_slots[idx].pos);
			}
			else if (node == IR0_EARLY_ROOTFS_DEV && vda_present() &&
				 g_slots[idx].pos == syn)
			{
				spec.name = "vda";
				spec.ino = 8;
				spec.type = IR0_EARLY_DT_BLK;
			}
			else
			{
				unsigned skip = syn;

				if (node == IR0_EARLY_ROOTFS_DEV && vda_present())
					skip++;
				if (!fs_child_at(dirpath,
						 (int)(g_slots[idx].pos - skip),
						 &spec))
					break;
			}
		if (!spec.name)
			break;
		reclen = ir0_early_dirent64_pack(rec, sizeof(rec), spec.ino,
						(int64_t)(g_slots[idx].pos + 1),
						spec.type, spec.name);
		if (reclen == 0)
			return -EINVAL;
		if ((uint64_t)reclen > count - written)
			break;
		if (tmp_used + reclen > sizeof(tmp))
			break;
		for (i = 0; i < reclen; i++)
			tmp[tmp_used + i] = rec[i];
		tmp_used += reclen;
		written += reclen;
			g_slots[idx].pos++;
		}
		if (written == 0)
			return 0;
		if (!g_user || !g_user->copy_to_user ||
		    g_user->copy_to_user(buffer, tmp, written) != 0)
			return -EFAULT;
		return (int64_t)written;
	}
}

int early_rootfs_dents_ready(void)
{
	uint8_t rec[32];
	unsigned i;
	unsigned n;

	if (!g_ready)
		return 0;
	n = ir0_early_rootfs_dir_count(IR0_EARLY_ROOTFS_ROOT);
	if (n != 5 || ir0_early_rootfs_dir_count(IR0_EARLY_ROOTFS_DEV) != 6 ||
	    ir0_early_rootfs_dir_count(IR0_EARLY_ROOTFS_TMP) != 2)
		return 0;
	for (i = 0; i < n; i++)
	{
		struct ir0_early_dirent_spec spec;
		unsigned reclen;

		spec = ir0_early_rootfs_dir_entry(IR0_EARLY_ROOTFS_ROOT, i);
		if (!spec.name)
			return 0;
		reclen = ir0_early_dirent64_pack(rec, sizeof(rec), spec.ino,
						(int64_t)(i + 1), spec.type,
						spec.name);
		if (reclen < 20)
			return 0;
	}
	return 1;
}

int64_t early_rootfs_getcwd(uint64_t buffer, uint64_t size)
{
	const char *cwd;
	uint64_t n;

	if (!g_ready)
		return -ENOENT;
	if (g_cwd == IR0_EARLY_ROOTFS_DEV)
		cwd = (g_chroot == IR0_EARLY_ROOTFS_DEV) ? "/" : "/dev";
	else if (g_cwd == IR0_EARLY_ROOTFS_TMP)
		cwd = (g_chroot == IR0_EARLY_ROOTFS_TMP) ? "/" : "/tmp";
	else if (g_cwd == IR0_EARLY_ROOTFS_USERDIR)
		cwd = g_cwd_path[0] ? g_cwd_path : "/";
	else
		cwd = "/";
	n = 0;
	while (cwd[n] != '\0')
		n++;
	n++;
	if (size < n)
		return -ERANGE;
	if (!g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(buffer, cwd, n) != 0)
		return -EFAULT;
	return (int64_t)n;
}

int64_t early_rootfs_lseek(int fd, int64_t offset, int whence)
{
	enum ir0_early_rootfs_node node;
	uint64_t *pos;
	int64_t base;
	int64_t next;
	uint64_t limit;

	node = node_of_fd(fd);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -EBADF;
	if (node == IR0_EARLY_ROOTFS_CONSOLE || node == IR0_EARLY_ROOTFS_TTY ||
	    node == IR0_EARLY_ROOTFS_PIPE_R || node == IR0_EARLY_ROOTFS_PIPE_W ||
	    node == IR0_EARLY_ROOTFS_EVENTFD || node == IR0_EARLY_ROOTFS_TIMERFD ||
	    node == IR0_EARLY_ROOTFS_EPOLL || node == IR0_EARLY_ROOTFS_SOCK ||
	    node == IR0_EARLY_ROOTFS_FIFO)
		return -ESPIPE;
	pos = pos_of_fd(fd);
	if (!pos)
		return -EBADF;
	if (node == IR0_EARLY_ROOTFS_INIT)
		limit = g_init_vis;
	else if (node == IR0_EARLY_ROOTFS_VDA)
		limit = vda_bytes();
	else if (node == IR0_EARLY_ROOTFS_MEMFD)
	{
		int mid = pipe_id_of_fd(fd);

		limit = (mid >= 0 && mid < MEMFD_MAX && g_memfd[mid].used) ?
				g_memfd[mid].len :
				0;
	}
	else if (node == IR0_EARLY_ROOTFS_FILE)
	{
		int fsid = pipe_id_of_fd(fd);

		limit = (fsid >= 0 && fsid < FS_MAX && g_fs[fsid].used) ?
				g_fs[fsid].size :
				0;
	}
	else if (node_is_dir(node))
		limit = ir0_early_rootfs_dir_count(node);
	else
		limit = 0;
	if (whence == SEEK_SET)
		base = 0;
	else if (whence == SEEK_CUR)
		base = (int64_t)*pos;
	else if (whence == SEEK_END)
		base = (int64_t)limit;
	else
		return -EINVAL;
	next = base + offset;
	if (next < 0)
		return -EINVAL;
	*pos = (uint64_t)next;
	return next;
}

int64_t early_rootfs_fcntl(int fd, int cmd, uint64_t arg)
{
	int newfd;

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
		for (newfd = (int)arg; newfd < ROOTFS_FD_LIMIT; newfd++)
		{
			if (fd_is_open(newfd))
				continue;
			if (install_fd(newfd, node_of_fd(fd)) < 0)
				return -EBADF;
			set_flags_of_fd(newfd, flags_of_fd(fd));
			set_cloexec_of_fd(newfd, cmd == F_DUPFD_CLOEXEC);
			attach_object(fd, newfd);
			return newfd;
		}
		return -EMFILE;
	}
	if (cmd == F_GETLK || cmd == F_SETLK || cmd == F_SETLKW)
		return -EINVAL;
	return -EINVAL;
}

int64_t early_rootfs_fchdir(int fd)
{
	enum ir0_early_rootfs_node node = node_of_fd(fd);

	if (node == IR0_EARLY_ROOTFS_NONE)
		return -EBADF;
	if (!node_is_dir(node))
		return -ENOTDIR;
	g_cwd = node;
	if (node == IR0_EARLY_ROOTFS_USERDIR)
	{
		int fsid = pipe_id_of_fd(fd);
		unsigned i;

		if (fsid >= 0 && fsid < FS_MAX && g_fs[fsid].used)
		{
			for (i = 0; g_fs[fsid].path[i] != '\0' &&
				    i + 1 < sizeof(g_cwd_path); i++)
				g_cwd_path[i] = g_fs[fsid].path[i];
			g_cwd_path[i] = '\0';
		}
	}
	else if (node == IR0_EARLY_ROOTFS_DEV)
	{
		g_cwd_path[0] = '/';
		g_cwd_path[1] = 'd';
		g_cwd_path[2] = 'e';
		g_cwd_path[3] = 'v';
		g_cwd_path[4] = '\0';
	}
	else if (node == IR0_EARLY_ROOTFS_TMP)
	{
		g_cwd_path[0] = '/';
		g_cwd_path[1] = 't';
		g_cwd_path[2] = 'm';
		g_cwd_path[3] = 'p';
		g_cwd_path[4] = '\0';
	}
	else
	{
		g_cwd_path[0] = '/';
		g_cwd_path[1] = '\0';
	}
	return 0;
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
	ret = walk_user_vector(envp, 0);
	if (ret < 0)
		return ret;
	ret = walk_user_vector(argv, 1);
	if (ret < 0)
		return ret;
	g_exec_pending = 1;
	return 0;
}

int early_rootfs_exec_pending(void)
{
	return g_exec_pending;
}

char *const *early_rootfs_exec_argv(void)
{
	return g_exec_argv;
}

void early_rootfs_exec_clear(void)
{
	g_exec_pending = 0;
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

int early_rootfs_fd_is_pipe_write(int fd)
{
	return node_of_fd(fd) == IR0_EARLY_ROOTFS_PIPE_W;
}

int64_t early_rootfs_pipe_write(int fd, uint64_t buffer, uint64_t count)
{
	int pipe_id = pipe_id_of_fd(fd);
	struct early_pipe *p;
	uint8_t tmp[256];
	unsigned room;
	unsigned put;
	unsigned i;
	unsigned wpos;

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
		return -EAGAIN;
	put = room;
	if (put > sizeof(tmp))
		put = (unsigned)sizeof(tmp);
	if ((uint64_t)put > count)
		put = (unsigned)count;
	if (!g_user || !g_user->copy_from_user ||
	    g_user->copy_from_user(tmp, buffer, put) != 0)
		return -EFAULT;
	wpos = (p->rpos + p->len) % PIPE_BUF;
	for (i = 0; i < put; i++)
	{
		p->buf[wpos] = tmp[i];
		wpos = (wpos + 1U) % PIPE_BUF;
	}
	p->len += put;
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

int early_rootfs_fd_is_eventfd(int fd)
{
	return node_of_fd(fd) == IR0_EARLY_ROOTFS_EVENTFD;
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
		return -EAGAIN;
	g_eventfd[efd].count += add;
	return 8;
}

int early_rootfs_fd_is_memfd(int fd)
{
	return node_of_fd(fd) == IR0_EARLY_ROOTFS_MEMFD;
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

int early_rootfs_fd_is_sock(int fd)
{
	return node_of_fd(fd) == IR0_EARLY_ROOTFS_SOCK;
}

int early_rootfs_fd_is_file(int fd)
{
	return node_of_fd(fd) == IR0_EARLY_ROOTFS_FILE;
}

int64_t early_rootfs_file_write(int fd, uint64_t buffer, uint64_t count)
{
	int fsid = pipe_id_of_fd(fd);
	uint64_t *pos = pos_of_fd(fd);
	uint8_t tmp[256];
	uint64_t n;
	uint64_t i;
	uint64_t start;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_FILE)
		return -EBADF;
	if ((flags_of_fd(fd) & LINUX_O_ACCMODE) == 0)
		return -EBADF;
	if (fsid < 0 || fsid >= FS_MAX || !g_fs[fsid].used || !pos)
		return -EBADF;
	if (!g_fs[fsid].writable)
		return -EROFS;
	if (count == 0)
		return 0;
	start = *pos;
	if (start >= FS_FILE_CAP)
		return -EFBIG;
	n = count;
	if (start + n > FS_FILE_CAP)
		n = FS_FILE_CAP - start;
	if (n > sizeof(tmp))
		n = sizeof(tmp);
	if (!g_user || !g_user->copy_from_user ||
	    g_user->copy_from_user(tmp, buffer, n) != 0)
		return -EFAULT;
	for (i = 0; i < n; i++)
		g_fs[fsid].own[start + i] = tmp[i];
	*pos = start + n;
	if (*pos > g_fs[fsid].size)
		g_fs[fsid].size = (uint32_t)*pos;
	g_fs[fsid].data = g_fs[fsid].own;
	return (int64_t)n;
}

static int copy_sun_path(uint64_t addr, uint64_t addrlen, char *out,
			 unsigned cap)
{
	uint8_t raw[2 + IR0_EARLY_SUN_PATH];
	unsigned n;

	if (addrlen < 3 || addrlen > sizeof(raw))
		return 0;
	n = (unsigned)addrlen;
	if (!g_user || !g_user->copy_from_user ||
	    g_user->copy_from_user(raw, addr, n) != 0)
		return 0;
	return ir0_early_sockaddr_un_path(raw, n, out, cap);
}

static int64_t write_sun_name(uint64_t addr, uint64_t addrlenp,
			      const char *path)
{
	uint32_t want = 2;
	uint8_t fam[2];
	unsigned i;
	unsigned plen = 0;

	if (!addrlenp)
		return 0;
	if (!g_user || !g_user->copy_from_user || !g_user->copy_to_user)
		return -EFAULT;
	if (g_user->copy_from_user(&want, addrlenp, 4) != 0)
		return -EFAULT;
	if (!addr)
		return 0;
	if (path)
	{
		while (path[plen] != '\0')
			plen++;
		want = 2U + plen + 1U;
	}
	else
		want = 2;
	if (g_user->copy_to_user(addrlenp, &want, 4) != 0)
		return -EFAULT;
	fam[0] = (uint8_t)IR0_EARLY_AF_UNIX;
	fam[1] = 0;
	if (g_user->copy_to_user(addr, fam, 2) != 0)
		return -EFAULT;
	if (path)
	{
		for (i = 0; i <= plen && 2U + i < 2U + IR0_EARLY_SUN_PATH; i++)
		{
			uint8_t ch = (uint8_t)path[i];

			if (g_user->copy_to_user(addr + 2 + i, &ch, 1) != 0)
				return -EFAULT;
		}
	}
	return 0;
}

int64_t early_rootfs_socket(int domain, int type, int protocol)
{
	int uid;
	int fd;
	int idx;
	int kind = type & 0xf;

	if (!g_ready)
		return -ENOSYS;
	if (domain != IR0_EARLY_AF_UNIX)
		return -EAFNOSUPPORT;
	if (protocol != 0)
		return -EPROTONOSUPPORT;
	if (kind != IR0_EARLY_SOCK_STREAM && kind != IR0_EARLY_SOCK_DGRAM)
		return -ESOCKTNOSUPPORT;
	if (type & ~(0xf | IR0_EARLY_SOCK_CLOEXEC | IR0_EARLY_SOCK_NONBLOCK))
		return -EINVAL;
	for (uid = 0; uid < UNIX_MAX; uid++)
	{
		if (!g_unix[uid].used)
			break;
	}
	if (uid >= UNIX_MAX)
		return -EMFILE;
	fd = alloc_fd(IR0_EARLY_ROOTFS_SOCK);
	if (fd < 0)
		return fd;
	idx = slot_from_fd(fd);
	if (idx < 0)
	{
		close_fd(fd);
		return -EBADF;
	}
	g_unix[uid].used = 1;
	g_unix[uid].refs = 1;
	g_unix[uid].stream = (kind == IR0_EARLY_SOCK_STREAM);
	g_unix[uid].bound_fs = -1;
	g_unix[uid].listening = 0;
	g_unix[uid].pending_pair = -1;
	g_unix[uid].pending_end = 0;
	g_unix[uid].peer_fs = -1;
	g_unix[uid].bound_path[0] = '\0';
	g_unix[uid].rpos = 0;
	g_unix[uid].len = 0;
	g_slots[idx].pipe_id = UNIX_ID_BASE + uid;
	set_flags_of_fd(fd, IR0_O_RDWR |
			    ((type & IR0_EARLY_SOCK_NONBLOCK) ? LINUX_O_NONBLOCK :
							       0));
	set_cloexec_of_fd(fd, (type & IR0_EARLY_SOCK_CLOEXEC) != 0);
	return fd;
}

int64_t early_rootfs_sock_bind(int fd, uint64_t addr, uint64_t addrlen)
{
	char pbuf[FS_NAME];
	int uid = unix_id_of_fd(fd);
	int ret;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (uid < 0 || !g_unix[uid].used)
		return -EINVAL;
	if (g_unix[uid].bound_fs >= 0)
		return -EINVAL;
	if (!copy_sun_path(addr, addrlen, pbuf, sizeof(pbuf)))
		return -EINVAL;
	if (!join_under_cwd(pbuf, sizeof(pbuf)))
		return -ENAMETOOLONG;
	apply_chroot(pbuf, sizeof(pbuf));
	if (ir0_early_rootfs_path_node(pbuf) != IR0_EARLY_ROOTFS_NONE ||
	    fs_find(pbuf) >= 0)
		return -EADDRINUSE;
	if (!parent_exists(pbuf))
		return -ENOENT;
	ret = fs_add(pbuf, S_IFSOCK | 0666U, 0, 0, 0);
	if (ret < 0)
		return ret;
	g_unix[uid].bound_fs = ret;
	{
		unsigned i;

		for (i = 0; pbuf[i] != '\0' && i + 1 < sizeof(g_unix[uid].bound_path);
		     i++)
			g_unix[uid].bound_path[i] = pbuf[i];
		g_unix[uid].bound_path[i] = '\0';
	}
	return 0;
}

int64_t early_rootfs_sock_listen(int fd, int backlog)
{
	int uid = unix_id_of_fd(fd);

	(void)backlog;
	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (uid < 0 || !g_unix[uid].used)
		return -EINVAL;
	if (!g_unix[uid].stream)
		return -EOPNOTSUPP;
	if (g_unix[uid].bound_fs < 0)
		return -EINVAL;
	g_unix[uid].listening = 1;
	return 0;
}

int64_t early_rootfs_sock_connect(int fd, uint64_t addr, uint64_t addrlen)
{
	char pbuf[FS_NAME];
	int uid = unix_id_of_fd(fd);
	int dest;
	int pair;
	int idx;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (sock_pair_of_fd(fd) >= 0 && unix_id_of_fd(fd) < 0)
		return -EISCONN;
	if (uid < 0 || !g_unix[uid].used)
		return -EINVAL;
	if (g_unix[uid].listening)
		return -EINVAL;
	if (!copy_sun_path(addr, addrlen, pbuf, sizeof(pbuf)))
		return -EINVAL;
	if (!join_under_cwd(pbuf, sizeof(pbuf)))
		return -ENAMETOOLONG;
	apply_chroot(pbuf, sizeof(pbuf));
	if (!g_unix[uid].stream)
	{
		int fsid = fs_find(pbuf);

		if (fsid < 0 ||
		    fs_node_kind(g_fs[fsid].mode) != IR0_EARLY_ROOTFS_UNIX)
			return -ECONNREFUSED;
		g_unix[uid].peer_fs = fsid;
		return 0;
	}
	if (g_unix[uid].bound_fs >= 0 &&
	    sock_pair_of_fd(fd) >= 0)
		return -EISCONN;
	dest = unix_find_bound(pbuf, 1);
	if (dest < 0 || !g_unix[dest].listening)
		return -ECONNREFUSED;
	if (g_unix[dest].pending_pair >= 0)
		return -ECONNREFUSED;
	for (pair = 0; pair < SOCKPAIR_MAX; pair++)
	{
		if (!g_sock[pair].used)
			break;
	}
	if (pair >= SOCKPAIR_MAX)
		return -EMFILE;
	idx = slot_from_fd(fd);
	if (idx < 0)
		return -EBADF;
	g_sock[pair].used = 1;
	g_sock[pair].refs[0] = 1;
	g_sock[pair].refs[1] = 1;
	g_sock[pair].dgram = 0;
	g_sock[pair].dir[0].rpos = 0;
	g_sock[pair].dir[0].len = 0;
	g_sock[pair].dir[1].rpos = 0;
	g_sock[pair].dir[1].len = 0;
	g_unix[dest].pending_pair = pair;
	g_unix[dest].pending_end = 1;
	g_unix[uid].refs--;
	if (g_unix[uid].refs <= 0)
		g_unix[uid].used = 0;
	g_slots[idx].pipe_id = pair << 1;
	return 0;
}

int64_t early_rootfs_sock_accept4(int fd, uint64_t addr, uint64_t addrlen,
				  int flags)
{
	int uid = unix_id_of_fd(fd);
	int newfd;
	int idx;
	int pair;
	int end;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (flags & ~(IR0_EARLY_SOCK_CLOEXEC | IR0_EARLY_SOCK_NONBLOCK))
		return -EINVAL;
	if (uid < 0 || !g_unix[uid].used || !g_unix[uid].listening)
		return -EINVAL;
	if (!g_unix[uid].stream)
		return -EOPNOTSUPP;
	if (g_unix[uid].pending_pair < 0)
		return -EAGAIN;
	pair = g_unix[uid].pending_pair;
	end = g_unix[uid].pending_end;
	g_unix[uid].pending_pair = -1;
	newfd = alloc_fd(IR0_EARLY_ROOTFS_SOCK);
	if (newfd < 0)
	{
		g_unix[uid].pending_pair = pair;
		return newfd;
	}
	idx = slot_from_fd(newfd);
	if (idx < 0)
	{
		close_fd(newfd);
		g_unix[uid].pending_pair = pair;
		return -EBADF;
	}
	g_slots[idx].pipe_id = (pair << 1) | end;
	set_flags_of_fd(newfd, IR0_O_RDWR |
				((flags & IR0_EARLY_SOCK_NONBLOCK) ?
					 LINUX_O_NONBLOCK :
					 0));
	set_cloexec_of_fd(newfd, (flags & IR0_EARLY_SOCK_CLOEXEC) != 0);
	if (write_sun_name(addr, addrlen, 0) < 0)
	{
		close_fd(newfd);
		return -EFAULT;
	}
	return newfd;
}

int64_t early_rootfs_getsockname(int fd, uint64_t addr, uint64_t addrlen)
{
	int uid = unix_id_of_fd(fd);

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (uid >= 0 && g_unix[uid].used && g_unix[uid].bound_path[0])
		return write_sun_name(addr, addrlen, g_unix[uid].bound_path);
	return write_sun_name(addr, addrlen, 0);
}

int64_t early_rootfs_getpeername(int fd, uint64_t addr, uint64_t addrlen)
{
	int uid = unix_id_of_fd(fd);

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (uid >= 0)
	{
		if (!g_unix[uid].used)
			return -EBADF;
		if (g_unix[uid].stream || g_unix[uid].peer_fs < 0)
			return -ENOTCONN;
		if (g_unix[uid].peer_fs >= FS_MAX ||
		    !g_fs[g_unix[uid].peer_fs].used)
			return -ENOTCONN;
		return write_sun_name(addr, addrlen,
				      g_fs[g_unix[uid].peer_fs].path);
	}
	if (sock_pair_of_fd(fd) < 0)
		return -ENOTCONN;
	return write_sun_name(addr, addrlen, 0);
}

int64_t early_rootfs_sendto(int fd, uint64_t buffer, uint64_t count,
			    uint64_t dest, uint64_t destlen)
{
	int uid = unix_id_of_fd(fd);
	char pbuf[FS_NAME];
	uint8_t tmp[256];
	int dest_uid;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (uid >= 0 && !g_unix[uid].stream && dest && destlen)
	{
		if (!copy_sun_path(dest, destlen, pbuf, sizeof(pbuf)))
			return -EINVAL;
		if (!join_under_cwd(pbuf, sizeof(pbuf)))
			return -ENAMETOOLONG;
		apply_chroot(pbuf, sizeof(pbuf));
		dest_uid = unix_find_bound(pbuf, 0);
		if (dest_uid < 0)
			return -ECONNREFUSED;
		if (count == 0)
			return 0;
		if (count > 250)
			return -EMSGSIZE;
		if (!g_user || !g_user->copy_from_user ||
		    g_user->copy_from_user(tmp, buffer, count) != 0)
			return -EFAULT;
		return unix_dgram_deposit(dest_uid, tmp, (unsigned)count);
	}
	if (uid >= 0 && g_unix[uid].stream && dest && destlen)
		return -EISCONN;
	return early_rootfs_sock_write(fd, buffer, count);
}

int64_t early_rootfs_recvfrom(int fd, uint64_t buffer, uint64_t count)
{
	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	return early_rootfs_read(fd, buffer, count);
}

int early_rootfs_fd_is_fifo(int fd)
{
	return node_of_fd(fd) == IR0_EARLY_ROOTFS_FIFO;
}

int64_t early_rootfs_fifo_write(int fd, uint64_t buffer, uint64_t count)
{
	int fsid = pipe_id_of_fd(fd);
	int pipe_id;
	struct early_pipe *p;
	uint8_t tmp[256];
	unsigned room;
	unsigned put;
	unsigned i;
	unsigned wpos;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_FIFO)
		return -EBADF;
	if ((flags_of_fd(fd) & LINUX_O_ACCMODE) == 0)
		return -EBADF;
	if (fsid < 0 || fsid >= FS_MAX || !g_fs[fsid].used)
		return -EBADF;
	pipe_id = g_fs[fsid].fifo_pipe;
	if (pipe_id < 0 || pipe_id >= PIPE_MAX)
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
		return -EAGAIN;
	put = room;
	if (put > sizeof(tmp))
		put = (unsigned)sizeof(tmp);
	if ((uint64_t)put > count)
		put = (unsigned)count;
	if (!g_user || !g_user->copy_from_user ||
	    g_user->copy_from_user(tmp, buffer, put) != 0)
		return -EFAULT;
	wpos = (p->rpos + p->len) % PIPE_BUF;
	for (i = 0; i < put; i++)
	{
		p->buf[wpos] = tmp[i];
		wpos = (wpos + 1U) % PIPE_BUF;
	}
	p->len += put;
	return (int64_t)put;
}

int64_t early_rootfs_socketpair(int domain, int type, int protocol, uint64_t sv)
{
	int pair;
	int a;
	int b;
	int aidx;
	int bidx;
	int fds[2];
	int kind = type & 0xf;

	if (!g_ready)
		return -ENOSYS;
	if (domain != IR0_EARLY_AF_UNIX)
		return -EAFNOSUPPORT;
	if (protocol != 0)
		return -EPROTONOSUPPORT;
	if (kind != IR0_EARLY_SOCK_STREAM && kind != IR0_EARLY_SOCK_DGRAM)
		return -ESOCKTNOSUPPORT;
	if (type & ~(0xf | IR0_EARLY_SOCK_CLOEXEC | IR0_EARLY_SOCK_NONBLOCK))
		return -EINVAL;
	if (!sv || !g_user || !g_user->copy_to_user)
		return -EFAULT;
	for (pair = 0; pair < SOCKPAIR_MAX; pair++)
	{
		if (!g_sock[pair].used)
			break;
	}
	if (pair >= SOCKPAIR_MAX)
		return -EMFILE;
	a = alloc_fd(IR0_EARLY_ROOTFS_SOCK);
	if (a < 0)
		return a;
	b = alloc_fd(IR0_EARLY_ROOTFS_SOCK);
	if (b < 0)
	{
		close_fd(a);
		return b;
	}
	aidx = slot_from_fd(a);
	bidx = slot_from_fd(b);
	if (aidx < 0 || bidx < 0)
	{
		close_fd(a);
		close_fd(b);
		return -EBADF;
	}
	g_sock[pair].used = 1;
	g_sock[pair].refs[0] = 1;
	g_sock[pair].refs[1] = 1;
	g_sock[pair].dgram = (kind == IR0_EARLY_SOCK_DGRAM);
	g_sock[pair].dir[0].rpos = 0;
	g_sock[pair].dir[0].len = 0;
	g_sock[pair].dir[1].rpos = 0;
	g_sock[pair].dir[1].len = 0;
	g_slots[aidx].pipe_id = pair << 1;
	g_slots[bidx].pipe_id = (pair << 1) | 1;
	set_flags_of_fd(a, IR0_O_RDWR |
			   ((type & IR0_EARLY_SOCK_NONBLOCK) ? LINUX_O_NONBLOCK :
							      0));
	set_flags_of_fd(b, IR0_O_RDWR |
			   ((type & IR0_EARLY_SOCK_NONBLOCK) ? LINUX_O_NONBLOCK :
							      0));
	set_cloexec_of_fd(a, (type & IR0_EARLY_SOCK_CLOEXEC) != 0);
	set_cloexec_of_fd(b, (type & IR0_EARLY_SOCK_CLOEXEC) != 0);
	fds[0] = a;
	fds[1] = b;
	if (g_user->copy_to_user(sv, fds, sizeof(fds)) != 0)
	{
		close_fd(a);
		close_fd(b);
		return -EFAULT;
	}
	return 0;
}

static int64_t unix_dgram_deposit(int uid, const uint8_t *tmp, unsigned count)
{
	unsigned room;
	unsigned wpos;
	unsigned i;

	if (uid < 0 || uid >= UNIX_MAX || !g_unix[uid].used || g_unix[uid].stream)
		return -ECONNREFUSED;
	if (count > 250)
		return -EMSGSIZE;
	room = PIPE_BUF - g_unix[uid].len;
	if (room < count + 2U)
		return -EAGAIN;
	wpos = (g_unix[uid].rpos + g_unix[uid].len) % PIPE_BUF;
	g_unix[uid].buf[wpos] = (uint8_t)(count & 0xffU);
	wpos = (wpos + 1U) % PIPE_BUF;
	g_unix[uid].buf[wpos] = (uint8_t)((count >> 8) & 0xffU);
	wpos = (wpos + 1U) % PIPE_BUF;
	for (i = 0; i < count; i++)
	{
		g_unix[uid].buf[wpos] = tmp[i];
		wpos = (wpos + 1U) % PIPE_BUF;
	}
	g_unix[uid].len += count + 2U;
	return (int64_t)count;
}

static int unix_find_bound(const char *path, int stream)
{
	int fsid;
	int i;

	fsid = fs_find(path);
	if (fsid < 0 || fs_node_kind(g_fs[fsid].mode) != IR0_EARLY_ROOTFS_UNIX)
		return -1;
	for (i = 0; i < UNIX_MAX; i++)
	{
		if (g_unix[i].used && g_unix[i].bound_fs == fsid &&
		    g_unix[i].stream == stream)
			return i;
	}
	return -1;
}

int64_t early_rootfs_sock_write(int fd, uint64_t buffer, uint64_t count)
{
	int encoded = pipe_id_of_fd(fd);
	int uid = unix_id_of_fd(fd);
	int pair = encoded >> 1;
	int end = encoded & 1;
	int dst = end;
	uint8_t tmp[256];
	unsigned room;
	unsigned put;
	unsigned i;
	unsigned wpos;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -EBADF;
	if (uid >= 0)
	{
		if (!g_unix[uid].used)
			return -EBADF;
		if (g_unix[uid].stream)
			return -ENOTCONN;
		if (g_unix[uid].peer_fs < 0 ||
		    g_unix[uid].peer_fs >= FS_MAX ||
		    !g_fs[g_unix[uid].peer_fs].used)
			return -ENOTCONN;
		{
			int dest = unix_find_bound(g_fs[g_unix[uid].peer_fs].path,
						   0);

			if (dest < 0)
				return -ECONNREFUSED;
			if (count == 0)
				return 0;
			if (count > 250)
				return -EMSGSIZE;
			if (!g_user || !g_user->copy_from_user ||
			    g_user->copy_from_user(tmp, buffer, count) != 0)
				return -EFAULT;
			return unix_dgram_deposit(dest, tmp, (unsigned)count);
		}
	}
	if (pair < 0 || pair >= SOCKPAIR_MAX || !g_sock[pair].used)
		return -EBADF;
	if (g_sock[pair].refs[1 - end] == 0)
		return -EPIPE;
	if (count == 0)
		return 0;
	room = PIPE_BUF - g_sock[pair].dir[dst].len;
	if (g_sock[pair].dgram)
	{
		if (count > 250)
			return -EMSGSIZE;
		if (room < count + 2)
			return -EAGAIN;
		if (!g_user || !g_user->copy_from_user ||
		    g_user->copy_from_user(tmp, buffer, count) != 0)
			return -EFAULT;
		wpos = (g_sock[pair].dir[dst].rpos + g_sock[pair].dir[dst].len) %
		       PIPE_BUF;
		g_sock[pair].dir[dst].buf[wpos] = (uint8_t)(count & 0xffU);
		wpos = (wpos + 1U) % PIPE_BUF;
		g_sock[pair].dir[dst].buf[wpos] = (uint8_t)((count >> 8) & 0xffU);
		wpos = (wpos + 1U) % PIPE_BUF;
		for (i = 0; i < count; i++)
		{
			g_sock[pair].dir[dst].buf[wpos] = tmp[i];
			wpos = (wpos + 1U) % PIPE_BUF;
		}
		g_sock[pair].dir[dst].len += (unsigned)count + 2U;
		return (int64_t)count;
	}
	if (room == 0)
		return -EAGAIN;
	put = room;
	if (put > sizeof(tmp))
		put = (unsigned)sizeof(tmp);
	if ((uint64_t)put > count)
		put = (unsigned)count;
	if (!g_user || !g_user->copy_from_user ||
	    g_user->copy_from_user(tmp, buffer, put) != 0)
		return -EFAULT;
	wpos = (g_sock[pair].dir[dst].rpos + g_sock[pair].dir[dst].len) % PIPE_BUF;
	for (i = 0; i < put; i++)
	{
		g_sock[pair].dir[dst].buf[wpos] = tmp[i];
		wpos = (wpos + 1U) % PIPE_BUF;
	}
	g_sock[pair].dir[dst].len += put;
	return (int64_t)put;
}

int64_t early_rootfs_blkgetsize64(int fd, uint64_t dest)
{
	uint64_t bytes;

	if (!early_rootfs_fd_is_block(fd) || !vda_present())
		return -ENOTTY;
	if (!dest || !g_user || !g_user->copy_to_user)
		return -EFAULT;
	bytes = vda_bytes();
	return g_user->copy_to_user(dest, &bytes, sizeof(bytes)) == 0 ?
	       0 : -EFAULT;
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

		if (fsid < 0 || fsid >= FS_MAX || !g_fs[fsid].used)
			return IR0_EARLY_POLLNVAL;
		fifo = g_fs[fsid].fifo_pipe;
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

int64_t early_rootfs_fchmod(int fd, uint64_t mode)
{
	(void)mode;
	if (!g_ready || !fd_is_open(fd))
		return -EBADF;
	return 0;
}

int64_t early_rootfs_fchmodat(int dirfd, uint64_t path, uint64_t mode)
{
	char pbuf[32];
	enum ir0_early_rootfs_node node;

	(void)mode;
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
	if (node_is_dir(node))
		return -EISDIR;
	if (node == IR0_EARLY_ROOTFS_FILE)
	{
		int fsid = fs_find(pbuf);

		if (fsid < 0)
			return -ENOENT;
		if (!g_fs[fsid].writable)
			return -EROFS;
		if (length > FS_FILE_CAP)
			return -EFBIG;
		if ((uint32_t)length > g_fs[fsid].size)
		{
			uint32_t i;

			for (i = g_fs[fsid].size; i < (uint32_t)length; i++)
				g_fs[fsid].own[i] = 0;
		}
		g_fs[fsid].size = (uint32_t)length;
		g_fs[fsid].data = g_fs[fsid].own;
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

		if (fsid < 0 || fsid >= FS_MAX || !g_fs[fsid].used)
			return -EBADF;
		if (!g_fs[fsid].writable)
			return -EROFS;
		if (length > FS_FILE_CAP)
			return -EFBIG;
		n = (uint32_t)length;
		if (n > g_fs[fsid].size)
		{
			for (i = g_fs[fsid].size; i < n; i++)
				g_fs[fsid].own[i] = 0;
		}
		g_fs[fsid].size = n;
		g_fs[fsid].data = g_fs[fsid].own;
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

static int parent_exists(const char *path)
{
	char parent[FS_NAME];

	if (!fs_parent(path, parent, sizeof(parent)))
		return 0;
	if (ir0_early_rootfs_path_node(parent) != IR0_EARLY_ROOTFS_NONE)
		return 1;
	return fs_find(parent) >= 0;
}

static int join_under_cwd(char *pbuf, unsigned cap)
{
	char joined[FS_NAME];
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

int64_t early_rootfs_mkdirat(int dirfd, uint64_t path)
{
	char pbuf[FS_NAME];
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
	    fs_find(pbuf) >= 0)
		return -EEXIST;
	if (!parent_exists(pbuf))
		return -ENOENT;
	ret = fs_add(pbuf, S_IFDIR | 0755U, 0, 0, 0);
	return ret < 0 ? ret : 0;
}

int64_t early_rootfs_mknodat(int dirfd, uint64_t path, uint64_t mode)
{
	char pbuf[FS_NAME];
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
	    fs_find(pbuf) >= 0)
		return -EEXIST;
	if (!parent_exists(pbuf))
		return -ENOENT;
	ret = fs_add(pbuf, (uint32_t)mode, 0, 0,
		     ((uint32_t)mode & S_IFMT) == S_IFREG);
	return ret < 0 ? ret : 0;
}

int64_t early_rootfs_unlinkat(int dirfd, uint64_t path, int flags)
{
	char pbuf[FS_NAME];
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
	fsid = fs_find(pbuf);
	if (node_is_dir(node))
	{
		if (!(flags & AT_REMOVEDIR))
			return -EISDIR;
		if (fsid < 0)
			return -EBUSY;
		if (fs_child_count(pbuf) > 0)
			return -ENOTEMPTY;
		g_fs[fsid].used = 0;
		return 0;
	}
	if (flags & AT_REMOVEDIR)
		return -ENOTDIR;
	if (fsid < 0)
		return -EPERM;
	g_fs[fsid].used = 0;
	g_fs[fsid].fifo_pipe = -1;
	return 0;
}

int64_t early_rootfs_renameat(int olddirfd, uint64_t oldpath, int newdirfd,
			      uint64_t newpath)
{
	char oldbuf[FS_NAME];
	char newbuf[FS_NAME];
	enum ir0_early_rootfs_node node;
	int fsid;
	int dest;
	unsigned i;

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
	fsid = fs_find(oldbuf);
	if (fsid < 0)
		return -EACCES;
	if (ir0_early_rootfs_path_node(newbuf) != IR0_EARLY_ROOTFS_NONE)
		return -EACCES;
	if (!parent_exists(newbuf))
		return -ENOENT;
	dest = fs_find(newbuf);
	if (dest >= 0)
	{
		if (fs_node_kind(g_fs[dest].mode) == IR0_EARLY_ROOTFS_USERDIR)
			return -EISDIR;
		g_fs[dest].used = 0;
	}
	for (i = 0; newbuf[i] != '\0' && i + 1 < FS_NAME; i++)
		g_fs[fsid].path[i] = newbuf[i];
	if (newbuf[i] != '\0')
		return -ENAMETOOLONG;
	g_fs[fsid].path[i] = '\0';
	if (node == IR0_EARLY_ROOTFS_UNIX)
	{
		for (i = 0; i < UNIX_MAX; i++)
		{
			if (g_unix[i].used && g_unix[i].bound_fs == fsid)
			{
				unsigned n;

				for (n = 0; newbuf[n] != '\0' &&
					    n + 1 < sizeof(g_unix[i].bound_path);
				     n++)
					g_unix[i].bound_path[n] = newbuf[n];
				g_unix[i].bound_path[n] = '\0';
			}
		}
	}
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
