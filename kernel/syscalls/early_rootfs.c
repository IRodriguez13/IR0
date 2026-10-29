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
#include <ir0/cwd.h>
#include <ir0/early_cpio.h>
#include <ir0/fd_install.h>
#include <ir0/early_process.h>
#include <ir0/early_rootfs.h>
#include <ir0/vfs_file.h>
#include <ir0/vfs_dispatch.h>
#include <ir0/ramtab.h>
#include <ir0/early_anonfd.h>
#include <ir0/early_unix.h>
#include <ir0/product_path.h>
#include <ir0/stat.h>
#include <ir0/fcntl.h>
#include <ir0/types.h>
#include <ir0/statfs.h>
#include <ir0/errno.h>

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
	struct vfs_file *vf;
};

#define PIPE_MAX EARLY_PIPE_MAX
#define PIPE_BUF EARLY_PIPE_BUF
#define EFD_MAX EARLY_EFD_MAX
#define EFD_CLOEXEC 02000000
#define EFD_NONBLOCK 00004000
#define EFD_SEMAPHORE 1
#define EFD_COUNT_MAX 0xfffffffffffffffeULL
#define MEMFD_MAX EARLY_MEMFD_MAX
#define MEMFD_CAP EARLY_MEMFD_CAP
#define TIMERFD_MAX EARLY_TIMERFD_MAX
#define EPOLL_MAX EARLY_EPOLL_MAX
#define EPOLL_SLOTS EARLY_EPOLL_SLOTS
#define g_pipes g_early_pipes
#define g_eventfd g_early_eventfd
#define g_memfd g_early_memfd
#define g_timerfd g_early_timerfd
#define g_epoll g_early_epoll
#define SOCKPAIR_MAX EARLY_SOCKPAIR_MAX
#define UNIX_MAX EARLY_UNIX_MAX
#define UNIX_ID_BASE EARLY_UNIX_ID_BASE
#define g_sock g_early_sock
#define g_unix g_early_unix
#define NS_PER_SEC 1000000000ULL

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
static const struct ir0_cwd_ops *g_cwd_ops;
static int g_cwd_used;
static const struct ir0_fd_install_ops *g_fd_ops;
static int g_vfs_file_used;
static int g_vfs_ops_used;
static int g_fd_used;
static const struct ir0_product_path_ops *g_product_path;
static int g_product_path_used;

static void early_publish_fd(int fd, enum ir0_early_rootfs_node node);
static int early_fdtable_ok(int fd);
static uint64_t g_init_vis;
static int g_init_from_block;
static int g_exec_pending;
static int g_exec_owner;
static int g_abstract_ok;
static char g_exec_args[EXEC_ARGC_MAX][EXEC_ARG_LEN];
static char *g_exec_argv[EXEC_ARGC_MAX + 1];
static uint64_t g_clock_mono;
static uint64_t g_clock_hz;
static int64_t g_clock_realtime_off;

#define XA_MAX 16

static struct early_xattr
{
	int used;
	enum ir0_early_rootfs_node node;
	int id;
	char name[IR0_EARLY_XATTR_NAME_MAX + 1];
	uint32_t vlen;
	uint8_t value[IR0_EARLY_XATTR_VALUE_MAX];
} g_xattr[XA_MAX];

#define RULESET_MAX 2
#define LL_RULE_MAX 4

static struct early_ruleset
{
	int used;
	int refs;
	uint64_t handled;
	int nrules;
	struct
	{
		uint64_t allowed;
		char path[RAMTAB_NAME];
	} rules[LL_RULE_MAX];
} g_ruleset[RULESET_MAX];

static int g_ll_on;
static uint64_t g_ll_handled;
static int g_ll_n;
static struct
{
	uint64_t allowed;
	char path[RAMTAB_NAME];
} g_ll_rules[LL_RULE_MAX];

static int landlock_allow(const char *path, uint64_t need);
static int landlock_open_need(const char *path, int flags);
static int path_of_fd(int fd, char *out, unsigned cap);

static int g_cpio_ready;
static char g_cwd_path[RAMTAB_NAME];
static char g_chroot_path[RAMTAB_NAME];

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

static int64_t wait_or_eagain(int fd)
{
	if (fd >= 0 && (flags_of_fd(fd) & LINUX_O_NONBLOCK))
		return -EAGAIN;
	(void)early_process_request_yield();
	return -EAGAIN;
}

static struct early_pipe *pipe_of_fd(int fd)
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

#define pipe_consume early_pipe_consume
#define pipe_peek early_pipe_peek
#define pipe_deposit early_pipe_deposit

static int64_t splice_wait(int fd, unsigned flags)
{
	if (flags & IR0_EARLY_SPLICE_F_NONBLOCK)
		return -EAGAIN;
	return wait_or_eagain(fd);
}

static int load_loff(uint64_t uptr, int64_t *out, int64_t fallback)
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

static int store_loff(uint64_t uptr, int64_t value)
{
	if (!uptr)
		return 0;
	if (!g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(uptr, &value, sizeof(value)) != 0)
		return -EFAULT;
	return 0;
}

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

static void early_unix_rehome(int old_id, int new_id)
{
	int i;

	for (i = 0; i < UNIX_MAX; i++)
	{
		if (g_unix[i].used && g_unix[i].bound_fs == old_id)
			g_unix[i].bound_fs = new_id;
	}
}

int ir0_product_path_is_tmp(const char *path)
{
	unsigned i;

	if (!path)
		return 0;
	for (i = 0; IR0_PRODUCT_PATH_TMP_PREF[i] != '\0'; i++)
	{
		if (path[i] != IR0_PRODUCT_PATH_TMP_PREF[i])
			return 0;
	}
	return path[i] != '\0';
}

void early_rootfs_bind_product_path(const struct ir0_product_path_ops *ops)
{
	g_product_path = ops;
	g_product_path_used = 0;
}

const struct ir0_product_path_ops *ir0_product_path_bound(void)
{
	return g_product_path;
}

int early_rootfs_product_path_used(void)
{
	return g_product_path_used;
}

static int product_path_live(void)
{
	stat_t st;

	if (!g_product_path || !g_product_path->stat)
		return 0;
	return g_product_path->stat("/tmp", &st) == 0;
}

static enum ir0_early_rootfs_node product_lookup(const char *path)
{
	stat_t st;

	if (!product_path_live() || !ir0_product_path_is_tmp(path))
		return IR0_EARLY_ROOTFS_NONE;
	if (g_product_path->stat(path, &st) != 0)
		return IR0_EARLY_ROOTFS_NONE;
	g_product_path_used = 1;
	if ((st.st_mode & S_IFMT) == S_IFDIR)
		return IR0_EARLY_ROOTFS_USERDIR;
	return IR0_EARLY_ROOTFS_FILE;
}

static int64_t product_attach_open(int fd, const char *path, int flags)
{
	struct vfs_file *vf = 0;
	int sidx;
	int rc;

	if (!g_product_path || !g_product_path->open)
		return -ENODEV;
	rc = g_product_path->open(path, flags, 0644, &vf);
	if (rc < 0)
		return rc;
	sidx = slot_from_fd(fd);
	if (sidx < 0)
	{
		if (g_product_path->put && vf)
			(void)g_product_path->put(vf);
		return -EBADF;
	}
	g_slots[sidx].vf = vf;
	g_product_path_used = 1;
	return 0;
}

static struct vfs_file *vf_of_fd(int fd)
{
	int idx = slot_from_fd(fd);

	if (idx < 0)
		return 0;
	return g_slots[idx].vf;
}

static int64_t product_xfer(int fd, uint64_t buffer, uint64_t count, int write)
{
	struct vfs_file *vf = vf_of_fd(fd);
	uint8_t tmp[256];
	int n;

	if (!vf || !g_product_path)
		return -ENODEV;
	if (count == 0)
		return 0;
	n = (int)(count > sizeof(tmp) ? sizeof(tmp) : count);
	if (write)
	{
		if (!g_product_path->write)
			return -ENODEV;
		if (!g_user || !g_user->copy_from_user ||
		    g_user->copy_from_user(tmp, buffer, (uint64_t)n) != 0)
			return -EFAULT;
		n = g_product_path->write(vf, tmp, (size_t)n);
	}
	else
	{
		if (!g_product_path->read)
			return -ENODEV;
		n = g_product_path->read(vf, tmp, (size_t)n);
		if (n > 0 && (!g_user || !g_user->copy_to_user ||
			      g_user->copy_to_user(buffer, tmp, (uint64_t)n) != 0))
			return -EFAULT;
	}
	if (n < 0)
		return n;
	g_product_path_used = 1;
	return n;
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
		g_slots[i].vf = 0;
	}
	early_anonfd_reset();
	early_unix_reset();
	g_clock_mono = 0;
	g_clock_hz = 0;
	g_clock_realtime_off = 0;
	ramtab_reset();
	ramtab_set_rehome_observer(early_unix_rehome);
	for (i = 0; i < XA_MAX; i++)
		g_xattr[i].used = 0;
	for (i = 0; i < RULESET_MAX; i++)
	{
		g_ruleset[i].used = 0;
		g_ruleset[i].refs = 0;
		g_ruleset[i].nrules = 0;
	}
	g_ll_on = 0;
	g_ll_handled = 0;
	g_ll_n = 0;
	g_cpio_ready = 0;
	g_cwd_path[0] = '/';
	g_cwd_path[1] = '\0';
	g_chroot_path[0] = '/';
	g_chroot_path[1] = '\0';
	reset_stdio();
	g_exec_pending = 0;
	g_exec_owner = 0;
	g_abstract_ok = 0;
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
			char norm[RAMTAB_NAME];
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
			char norm[RAMTAB_NAME];
			int ret;

			ret = ir0_early_cpio_next(buffer, bytes, off, &mem);
			if (ret <= 0)
				break;
			off = mem.next;
			if (!ir0_early_cpio_normalize(mem.name, norm, sizeof(norm)))
				continue;
			ramtab_add(norm, mem.mode, mem.filesize, mem.data, 0);
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
	node = product_lookup(pbuf);
	if (node != IR0_EARLY_ROOTFS_NONE)
		return node;
	idx = ramtab_find(pbuf);
	if (idx < 0)
		return IR0_EARLY_ROOTFS_NONE;
	return ramtab_node_kind(ramtab_table()[idx].mode);
}

static int follow_path(char *pbuf, unsigned cap)
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
		if (product_path_live() && ir0_product_path_is_tmp(pbuf) &&
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
	if (product_path_live() && ir0_product_path_is_tmp(pbuf) &&
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

static int early_vfs_ops_path_backed(enum ir0_early_rootfs_node node)
{
	return node == IR0_EARLY_ROOTFS_INIT ||
	       node == IR0_EARLY_ROOTFS_ZERO ||
	       node == IR0_EARLY_ROOTFS_NULL ||
	       node == IR0_EARLY_ROOTFS_CONSOLE ||
	       node == IR0_EARLY_ROOTFS_TTY ||
	       node == IR0_EARLY_ROOTFS_FILE;
}

static int early_vfs_ops_stat(const char *path, stat_t *st)
{
	enum ir0_early_rootfs_node node;
	struct ir0_early_stat64 raw;
	int fsid;

	if (!path || !st)
		return -EFAULT;
	node = ir0_early_rootfs_path_node(path);
	if (node != IR0_EARLY_ROOTFS_NONE)
	{
		ir0_early_rootfs_fill_stat(node, g_blob_len, &raw);
		st->st_dev = (dev_t)raw.st_dev;
		st->st_ino = (ino_t)raw.st_ino;
		st->st_mode = raw.st_mode;
		st->st_nlink = raw.st_nlink;
		st->st_uid = (uid_t)raw.st_uid;
		st->st_gid = (gid_t)raw.st_gid;
		st->st_rdev = (dev_t)raw.st_rdev;
		st->st_size = raw.st_size;
		st->st_blksize = raw.st_blksize;
		st->st_blocks = raw.st_blocks;
		st->st_atime = raw.st_atime_sec;
		st->st_mtime = raw.st_mtime_sec;
		st->st_ctime = raw.st_ctime_sec;
		return 0;
	}
	fsid = ramtab_find(path);
	if (fsid < 0)
		return -ENOENT;
	st->st_dev = 1;
	st->st_ino = (ino_t)(16 + fsid);
	st->st_mode = ramtab_table()[fsid].mode;
	st->st_nlink = 1;
	st->st_uid = 0;
	st->st_gid = 0;
	st->st_rdev = 0;
	st->st_size = (off_t)ramtab_table()[fsid].size;
	st->st_blksize = 4096;
	st->st_blocks = (blkcnt_t)((ramtab_table()[fsid].size + 511U) / 512U);
	st->st_atime = 0;
	st->st_mtime = 0;
	st->st_ctime = 0;
	return 0;
}

static int early_vfs_ops_read(const char *path, void *buf, size_t count,
			      size_t *bytes_read, off_t offset)
{
	enum ir0_early_rootfs_node node;
	uint8_t *dst = buf;
	size_t n;
	size_t i;
	int fsid;

	if (!bytes_read)
		return -EFAULT;
	*bytes_read = 0;
	if (offset < 0)
		return -EINVAL;
	if (count == 0)
		return 0;
	if (!buf)
		return -EFAULT;
	node = ir0_early_rootfs_path_node(path);
	if (node == IR0_EARLY_ROOTFS_ROOT ||
	    node == IR0_EARLY_ROOTFS_DEV ||
	    node == IR0_EARLY_ROOTFS_TMP)
		return -EISDIR;
	if (node == IR0_EARLY_ROOTFS_CONSOLE ||
	    node == IR0_EARLY_ROOTFS_TTY ||
	    node == IR0_EARLY_ROOTFS_NULL)
		return 0;
	if (node == IR0_EARLY_ROOTFS_ZERO)
	{
		n = count;
		for (i = 0; i < n; i++)
			dst[i] = 0;
		*bytes_read = n;
		return 0;
	}
	if (node == IR0_EARLY_ROOTFS_INIT)
	{
		if (!g_blob || (uint64_t)offset >= g_init_vis)
			return 0;
		n = (size_t)(g_init_vis - (uint64_t)offset);
		if (n > count)
			n = count;
		if ((uint64_t)offset >= g_blob_len)
			return 0;
		if (n > (size_t)(g_blob_len - (uint64_t)offset))
			n = (size_t)(g_blob_len - (uint64_t)offset);
		for (i = 0; i < n; i++)
			dst[i] = g_blob[(uint64_t)offset + i];
		*bytes_read = n;
		return 0;
	}
	fsid = ramtab_find(path);
	if (fsid < 0)
		return -ENOENT;
	if ((ramtab_table()[fsid].mode & IR0_EARLY_ROOTFS_S_IFMT) ==
	    IR0_EARLY_ROOTFS_S_IFDIR)
		return -EISDIR;
	if ((uint64_t)offset >= ramtab_table()[fsid].size)
		return 0;
	n = (size_t)(ramtab_table()[fsid].size - (uint64_t)offset);
	if (n > count)
		n = count;
	for (i = 0; i < n; i++)
		dst[i] = ramtab_table()[fsid].data ? ramtab_table()[fsid].data[(uint64_t)offset + i] :
					   0;
	*bytes_read = n;
	return 0;
}

static struct vfs_ops g_early_vfs_ops = {
	.stat = early_vfs_ops_stat,
	.read = early_vfs_ops_read,
};

static int64_t early_try_vfs_ops_read(int fd, enum ir0_early_rootfs_node node,
				      uint64_t buffer, uint64_t count)
{
	struct vfs_file *f;
	uint8_t tmp[256];
	size_t n;
	int ret;

	if (!early_vfs_ops_path_backed(node))
		return -ENODEV;
	if (!vfs_dispatch_bound() || !g_fd_ops || !g_fd_ops->file)
		return -ENODEV;
	f = g_fd_ops->file(fd);
	if (!f || !f->path[0])
		return -ENODEV;
	n = count > sizeof(tmp) ? sizeof(tmp) : (size_t)count;
	ret = vfs_dispatch_read(f, tmp, n);
	if (ret < 0)
		return ret;
	if (ret == 0)
	{
		g_vfs_ops_used = 1;
		return 0;
	}
	if (!g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(buffer, tmp, (uint64_t)ret) != 0)
		return -EFAULT;
	g_vfs_ops_used = 1;
	return (int64_t)ret;
}

void early_rootfs_bind_vfs_ops(void)
{
	g_vfs_ops_used = 0;
	(void)vfs_dispatch_bind(&g_early_vfs_ops);
}

int early_rootfs_vfs_ops_used(void)
{
	return g_vfs_ops_used;
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

static const char *early_cwd_string(void)
{
	if (g_cwd == IR0_EARLY_ROOTFS_DEV)
		return (g_chroot == IR0_EARLY_ROOTFS_DEV) ? "/" : "/dev";
	if (g_cwd == IR0_EARLY_ROOTFS_TMP)
		return (g_chroot == IR0_EARLY_ROOTFS_TMP) ? "/" : "/tmp";
	if (g_cwd == IR0_EARLY_ROOTFS_USERDIR)
		return g_cwd_path[0] ? g_cwd_path : "/";
	return "/";
}

static const char *early_root_string(void)
{
	if (g_chroot == IR0_EARLY_ROOTFS_DEV)
		return "/dev";
	if (g_chroot == IR0_EARLY_ROOTFS_TMP)
		return "/tmp";
	if (g_chroot_path[0])
		return g_chroot_path;
	return "/";
}

static void early_publish_cwd(void)
{
	if (g_cwd_ops && g_cwd_ops->set)
		g_cwd_ops->set(early_cwd_string());
}

static void early_publish_root(void)
{
	if (g_cwd_ops && g_cwd_ops->set_root)
		g_cwd_ops->set_root(early_root_string());
}

void early_rootfs_bind_cwd(const struct ir0_cwd_ops *ops)
{
	g_cwd_ops = ops;
	g_cwd_used = 0;
	early_publish_cwd();
	early_publish_root();
}

int early_rootfs_cwd_used(void)
{
	return g_cwd_used;
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

static int early_fdtable_ok(int fd)
{
	if (!g_fd_ops || !g_fd_ops->check)
		return 1;
	return g_fd_ops->check(fd) == 0;
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
	early_publish_cwd();
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
		char dirpath[RAMTAB_NAME];
		int denied;

		if (path_of_fd(fd, dirpath, sizeof(dirpath)) == 0)
		{
			denied = landlock_allow(dirpath,
						IR0_EARLY_LANDLOCK_FS_READ_DIR);
			if (denied < 0)
				return denied;
		}
	}
	{
		const char *dirpath = "/";
		unsigned base;
		int fsid = pipe_id_of_fd(fd);

		if (node == IR0_EARLY_ROOTFS_DEV)
			dirpath = "/dev";
		else if (node == IR0_EARLY_ROOTFS_TMP)
			dirpath = "/tmp";
		else if (node == IR0_EARLY_ROOTFS_USERDIR && fsid >= 0 &&
			 fsid < RAMTAB_MAX && ramtab_table()[fsid].used)
			dirpath = ramtab_table()[fsid].path;
		base = ir0_early_rootfs_dir_count(node);
		if (node == IR0_EARLY_ROOTFS_USERDIR && base == 0)
			base = 2;
		if (node == IR0_EARLY_ROOTFS_DEV && vda_present())
			base++;
		total = base + (unsigned)ramtab_child_count(dirpath);
		idx = slot_from_fd(fd);
		if (idx < 0)
			return -EBADF;
		{
			uint64_t *dpos = pos_of_fd(fd);

			if (!dpos)
				return -EBADF;
		if (count == 0)
			return -EINVAL;
		while (*dpos < total)
		{
			struct ir0_early_dirent_spec spec;
			unsigned reclen;
			unsigned i;
			unsigned syn = ir0_early_rootfs_dir_count(node);

			if (node == IR0_EARLY_ROOTFS_USERDIR && syn == 0)
				syn = 2;
			if (*dpos < syn)
			{
				if (node == IR0_EARLY_ROOTFS_USERDIR &&
				    ir0_early_rootfs_dir_count(node) == 0)
				{
					spec.name = (*dpos == 0) ? "." : "..";
					spec.ino = (*dpos == 0) ? 18 : 1;
					spec.type = IR0_EARLY_DT_DIR;
				}
				else
					spec = ir0_early_rootfs_dir_entry(node,
							 (unsigned)*dpos);
			}
			else if (node == IR0_EARLY_ROOTFS_DEV && vda_present() &&
				 *dpos == syn)
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
				if (!ramtab_child_at(dirpath,
						 (int)(*dpos - skip),
						 &spec))
					break;
			}
		if (!spec.name)
			break;
		reclen = ir0_early_dirent64_pack(rec, sizeof(rec), spec.ino,
						(int64_t)(*dpos + 1),
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
			(*dpos)++;
		}
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
	char task_cwd[256];
	uint64_t n;
	int got;

	if (!g_ready)
		return -ENOENT;
	if (g_cwd_ops && g_cwd_ops->get)
	{
		got = g_cwd_ops->get(task_cwd, sizeof(task_cwd));
		if (got > 0)
		{
			cwd = task_cwd;
			n = (uint64_t)got;
			if (size < n)
				return -ERANGE;
			if (!g_user || !g_user->copy_to_user ||
			    g_user->copy_to_user(buffer, cwd, n) != 0)
				return -EFAULT;
			g_cwd_used = 1;
			return (int64_t)n;
		}
	}
	cwd = early_cwd_string();
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

	if (vf_of_fd(fd) && g_product_path && g_product_path->lseek)
	{
		off_t got;

		got = g_product_path->lseek(vf_of_fd(fd), (off_t)offset, whence);
		if (got < 0)
			return (int64_t)got;
		g_product_path_used = 1;
		return (int64_t)got;
	}
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
		int fsid = ramtab_body(pipe_id_of_fd(fd));

		limit = (fsid >= 0 && fsid < RAMTAB_MAX && ramtab_table()[fsid].used) ?
				ramtab_table()[fsid].size :
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

		if (fsid >= 0 && fsid < RAMTAB_MAX && ramtab_table()[fsid].used)
		{
			for (i = 0; ramtab_table()[fsid].path[i] != '\0' &&
				    i + 1 < sizeof(g_cwd_path); i++)
				g_cwd_path[i] = ramtab_table()[fsid].path[i];
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
	early_publish_cwd();
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
		return wait_or_eagain(fd);
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

	if (vf_of_fd(fd))
		return product_xfer(fd, buffer, count, 1);
	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_FILE)
		return -EBADF;
	if ((flags_of_fd(fd) & LINUX_O_ACCMODE) == 0)
		return -EBADF;
	fsid = ramtab_body(fsid);
	if (fsid < 0 || fsid >= RAMTAB_MAX || !ramtab_table()[fsid].used || !pos)
		return -EBADF;
	if (!ramtab_table()[fsid].writable)
		return -EROFS;
	if (count == 0)
		return 0;
	start = *pos;
	if (start >= RAMTAB_FILE_CAP)
		return -EFBIG;
	n = count;
	if (start + n > RAMTAB_FILE_CAP)
		n = RAMTAB_FILE_CAP - start;
	if (n > sizeof(tmp))
		n = sizeof(tmp);
	if (!g_user || !g_user->copy_from_user ||
	    g_user->copy_from_user(tmp, buffer, n) != 0)
		return -EFAULT;
	for (i = 0; i < n; i++)
		ramtab_table()[fsid].own[start + i] = tmp[i];
	*pos = start + n;
	if (*pos > ramtab_table()[fsid].size)
		ramtab_table()[fsid].size = (uint32_t)*pos;
	ramtab_table()[fsid].data = ramtab_table()[fsid].own;
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

static int copy_sun_abs(uint64_t addr, uint64_t addrlen, uint8_t *out,
			unsigned *len, unsigned cap)
{
	uint8_t raw[2 + IR0_EARLY_SUN_PATH];
	unsigned n;
	unsigned i;

	if (addrlen < 3 || addrlen > sizeof(raw) || !out || !len)
		return 0;
	n = (unsigned)addrlen;
	if (!g_user || !g_user->copy_from_user ||
	    g_user->copy_from_user(raw, addr, n) != 0)
		return 0;
	if (!ir0_early_sockaddr_un_is_abstract(raw, n))
		return 0;
	*len = ir0_early_sockaddr_un_abs_len(n);
	if (*len > cap)
		*len = cap;
	for (i = 0; i < *len; i++)
		out[i] = raw[2U + i];
	return 1;
}

static int unix_find_abstract(const uint8_t *name, unsigned len, int stream)
{
	int i;
	unsigned k;

	if (!name)
		return -1;
	for (i = 0; i < UNIX_MAX; i++)
	{
		if (!g_unix[i].used || !g_unix[i].abstract ||
		    g_unix[i].stream != stream || g_unix[i].abs_len != len)
			continue;
		for (k = 0; k < len; k++)
		{
			if (g_unix[i].abs_name[k] != name[k])
				break;
		}
		if (k == len)
			return i;
	}
	return -1;
}

static void note_sender_creds_unix(int uid)
{
	if (uid < 0 || uid >= UNIX_MAX || !g_unix[uid].used)
		return;
	early_process_current_ucred(&g_unix[uid].cred_pid, &g_unix[uid].cred_uid,
				    &g_unix[uid].cred_gid);
	g_unix[uid].cred_valid = 1;
}

static void note_sender_creds_sock(int pair, int dest_end)
{
	if (pair < 0 || pair >= SOCKPAIR_MAX || dest_end < 0 || dest_end > 1 ||
	    !g_sock[pair].used)
		return;
	early_process_current_ucred(&g_sock[pair].cred_pid[dest_end],
				    &g_sock[pair].cred_uid[dest_end],
				    &g_sock[pair].cred_gid[dest_end]);
	g_sock[pair].cred_valid[dest_end] = 1;
}

static int64_t write_sun_abs(uint64_t addr, uint64_t addrlenp,
			     const uint8_t *name, unsigned n)
{
	uint32_t want;
	uint8_t fam[2];
	unsigned i;

	if (!addrlenp)
		return 0;
	if (!g_user || !g_user->copy_to_user)
		return -EFAULT;
	want = 2U + n;
	if (g_user->copy_to_user(addrlenp, &want, 4) != 0)
		return -EFAULT;
	if (!addr)
		return 0;
	fam[0] = (uint8_t)IR0_EARLY_AF_UNIX;
	fam[1] = 0;
	if (g_user->copy_to_user(addr, fam, 2) != 0)
		return -EFAULT;
	for (i = 0; i < n && i < IR0_EARLY_SUN_PATH; i++)
	{
		if (g_user->copy_to_user(addr + 2 + i, &name[i], 1) != 0)
			return -EFAULT;
	}
	return 0;
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
	g_unix[uid].reuseaddr = 0;
	g_unix[uid].so_error = 0;
	g_unix[uid].shut_rd = 0;
	g_unix[uid].shut_wr = 0;
	g_unix[uid].rights_n = 0;
	g_unix[uid].abstract = 0;
	g_unix[uid].abs_len = 0;
	g_unix[uid].peer_uid = -1;
	g_unix[uid].passcred = 0;
	g_unix[uid].cred_valid = 0;
	g_unix[uid].cred_explicit = 0;
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
	char pbuf[RAMTAB_NAME];
	uint8_t abs[IR0_EARLY_SUN_PATH];
	unsigned alen = 0;
	int uid = unix_id_of_fd(fd);
	int ret;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (uid < 0 || !g_unix[uid].used)
		return -EINVAL;
	if (g_unix[uid].bound_fs >= 0 || g_unix[uid].abstract)
		return -EINVAL;
	if (copy_sun_abs(addr, addrlen, abs, &alen, sizeof(abs)))
	{
		if (unix_find_abstract(abs, alen, g_unix[uid].stream) >= 0)
			return -EADDRINUSE;
		g_unix[uid].abstract = 1;
		g_unix[uid].abs_len = alen;
		g_abstract_ok = 1;
		{
			unsigned i;

			for (i = 0; i < alen; i++)
				g_unix[uid].abs_name[i] = abs[i];
		}
		return 0;
	}
	if (!copy_sun_path(addr, addrlen, pbuf, sizeof(pbuf)))
		return -EINVAL;
	if (!join_under_cwd(pbuf, sizeof(pbuf)))
		return -ENAMETOOLONG;
	apply_chroot(pbuf, sizeof(pbuf));
	if (ir0_early_rootfs_path_node(pbuf) != IR0_EARLY_ROOTFS_NONE ||
	    ramtab_find(pbuf) >= 0)
		return -EADDRINUSE;
	if (!parent_exists(pbuf))
		return -ENOENT;
	ret = ramtab_add(pbuf, S_IFSOCK | 0666U, 0, 0, 0);
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
	if (g_unix[uid].bound_fs < 0 && !g_unix[uid].abstract)
		return -EINVAL;
	g_unix[uid].listening = 1;
	return 0;
}

int64_t early_rootfs_sock_connect(int fd, uint64_t addr, uint64_t addrlen)
{
	char pbuf[RAMTAB_NAME];
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
	{
		uint8_t abs[IR0_EARLY_SUN_PATH];
		unsigned alen = 0;

		if (copy_sun_abs(addr, addrlen, abs, &alen, sizeof(abs)))
		{
			dest = unix_find_abstract(abs, alen, g_unix[uid].stream);
			if (dest < 0)
				return -ECONNREFUSED;
			if (!g_unix[uid].stream)
			{
				g_unix[uid].peer_uid = dest;
				return 0;
			}
			if (!g_unix[dest].listening)
				return -ECONNREFUSED;
		}
		else
		{
			if (!copy_sun_path(addr, addrlen, pbuf, sizeof(pbuf)))
				return -EINVAL;
			if (!join_under_cwd(pbuf, sizeof(pbuf)))
				return -ENAMETOOLONG;
			apply_chroot(pbuf, sizeof(pbuf));
			if (!g_unix[uid].stream)
			{
				int fsid = ramtab_find(pbuf);

				if (fsid < 0 ||
				    ramtab_node_kind(ramtab_table()[fsid].mode) !=
					    IR0_EARLY_ROOTFS_UNIX)
					return -ECONNREFUSED;
				g_unix[uid].peer_fs = fsid;
				return 0;
			}
			if (g_unix[uid].bound_fs >= 0 && sock_pair_of_fd(fd) >= 0)
				return -EISCONN;
			dest = unix_find_bound(pbuf, 1);
			if (dest < 0 || !g_unix[dest].listening)
				return -ECONNREFUSED;
		}
	}
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
	g_sock[pair].shut[0] = 0;
	g_sock[pair].shut[1] = 0;
	g_sock[pair].reuseaddr = 0;
	g_sock[pair].so_error = 0;
	g_sock[pair].rights_n[0] = 0;
	g_sock[pair].rights_n[1] = 0;
	g_sock[pair].passcred[0] = 0;
	g_sock[pair].passcred[1] = 0;
	g_sock[pair].cred_valid[0] = 0;
	g_sock[pair].cred_valid[1] = 0;
	g_sock[pair].cred_explicit[0] = 0;
	g_sock[pair].cred_explicit[1] = 0;
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
		return wait_or_eagain(fd);
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
	if (uid >= 0 && g_unix[uid].used && g_unix[uid].abstract)
		return write_sun_abs(addr, addrlen, g_unix[uid].abs_name,
				     g_unix[uid].abs_len);
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
		if (g_unix[uid].peer_fs >= RAMTAB_MAX ||
		    !ramtab_table()[g_unix[uid].peer_fs].used)
			return -ENOTCONN;
		return write_sun_name(addr, addrlen,
				      ramtab_table()[g_unix[uid].peer_fs].path);
	}
	if (sock_pair_of_fd(fd) < 0)
		return -ENOTCONN;
	return write_sun_name(addr, addrlen, 0);
}

int64_t early_rootfs_sendto(int fd, uint64_t buffer, uint64_t count,
			    uint64_t dest, uint64_t destlen)
{
	int uid = unix_id_of_fd(fd);
	char pbuf[RAMTAB_NAME];
	uint8_t tmp[256];
	int dest_uid;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (uid >= 0 && !g_unix[uid].stream && dest && destlen)
	{
		uint8_t abs[IR0_EARLY_SUN_PATH];
		unsigned alen = 0;

		if (copy_sun_abs(dest, destlen, abs, &alen, sizeof(abs)))
		{
			dest_uid = unix_find_abstract(abs, alen, 0);
			if (dest_uid < 0)
				return -ECONNREFUSED;
			if (count == 0)
				return 0;
			if (count > 250)
				return -EMSGSIZE;
			if (!g_user || !g_user->copy_from_user ||
			    g_user->copy_from_user(tmp, buffer, count) != 0)
				return -EFAULT;
			{
				int64_t sent = unix_dgram_deposit(dest_uid, tmp,
								  (unsigned)count);

				if (sent >= 0)
					g_abstract_ok = 1;
				return sent;
			}
		}
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

int64_t early_rootfs_shutdown(int fd, int how)
{
	int uid = unix_id_of_fd(fd);
	int pair = sock_pair_of_fd(fd);
	int end = pipe_id_of_fd(fd) & 1;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (how != IR0_EARLY_SHUT_RD && how != IR0_EARLY_SHUT_WR &&
	    how != IR0_EARLY_SHUT_RDWR)
		return -EINVAL;
	if (uid >= 0)
	{
		if (!g_unix[uid].used)
			return -EBADF;
		if (g_unix[uid].stream && g_unix[uid].listening)
			return -ENOTCONN;
		if (g_unix[uid].stream)
			return -ENOTCONN;
		if (how != IR0_EARLY_SHUT_WR && how != IR0_EARLY_SHUT_RDWR)
			g_unix[uid].shut_rd = 1;
		if (how != IR0_EARLY_SHUT_RD)
			g_unix[uid].shut_wr = 1;
		return 0;
	}
	if (pair < 0 || pair >= SOCKPAIR_MAX || !g_sock[pair].used)
		return -ENOTCONN;
	if (how != IR0_EARLY_SHUT_WR)
		g_sock[pair].shut[end] |= 1;
	if (how != IR0_EARLY_SHUT_RD)
		g_sock[pair].shut[end] |= 2;
	return 0;
}

int64_t early_rootfs_setsockopt(int fd, int level, int optname, uint64_t optval,
				uint64_t optlen)
{
	int uid = unix_id_of_fd(fd);
	int pair = sock_pair_of_fd(fd);
	int value = 0;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (level != IR0_EARLY_SOL_SOCKET)
		return -ENOPROTOOPT;
	if (optname != IR0_EARLY_SO_REUSEADDR &&
	    optname != IR0_EARLY_SO_PASSCRED)
		return -ENOPROTOOPT;
	if (optlen < 4 || !optval)
		return -EINVAL;
	if (!g_user || !g_user->copy_from_user ||
	    g_user->copy_from_user(&value, optval, 4) != 0)
		return -EFAULT;
	if (uid >= 0 && g_unix[uid].used)
	{
		if (optname == IR0_EARLY_SO_PASSCRED)
			g_unix[uid].passcred = value ? 1 : 0;
		else
			g_unix[uid].reuseaddr = value ? 1 : 0;
	}
	else if (pair >= 0 && pair < SOCKPAIR_MAX && g_sock[pair].used)
	{
		int end = pipe_id_of_fd(fd) & 1;

		if (optname == IR0_EARLY_SO_PASSCRED)
			g_sock[pair].passcred[end] = value ? 1 : 0;
		else
			g_sock[pair].reuseaddr = value ? 1 : 0;
	}
	else
		return -EBADF;
	return 0;
}

int64_t early_rootfs_getsockopt(int fd, int level, int optname, uint64_t optval,
				uint64_t optlen)
{
	int uid = unix_id_of_fd(fd);
	int pair = sock_pair_of_fd(fd);
	int value = 0;
	uint32_t len = 4;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (level != IR0_EARLY_SOL_SOCKET)
		return -ENOPROTOOPT;
	if (!optlen || !g_user || !g_user->copy_from_user ||
	    g_user->copy_from_user(&len, optlen, 4) != 0)
		return -EFAULT;
	if (len < 4 || !optval)
		return -EINVAL;
	if (uid >= 0 && g_unix[uid].used)
	{
		if (optname == IR0_EARLY_SO_TYPE)
			value = g_unix[uid].stream ? IR0_EARLY_SOCK_STREAM :
						     IR0_EARLY_SOCK_DGRAM;
		else if (optname == IR0_EARLY_SO_ERROR)
		{
			value = g_unix[uid].so_error;
			g_unix[uid].so_error = 0;
		}
		else if (optname == IR0_EARLY_SO_REUSEADDR)
			value = g_unix[uid].reuseaddr;
		else if (optname == IR0_EARLY_SO_PASSCRED)
			value = g_unix[uid].passcred;
		else
			return -ENOPROTOOPT;
	}
	else if (pair >= 0 && pair < SOCKPAIR_MAX && g_sock[pair].used)
	{
		if (optname == IR0_EARLY_SO_TYPE)
			value = g_sock[pair].dgram ? IR0_EARLY_SOCK_DGRAM :
						     IR0_EARLY_SOCK_STREAM;
		else if (optname == IR0_EARLY_SO_ERROR)
		{
			value = g_sock[pair].so_error;
			g_sock[pair].so_error = 0;
		}
		else if (optname == IR0_EARLY_SO_REUSEADDR)
			value = g_sock[pair].reuseaddr;
		else if (optname == IR0_EARLY_SO_PASSCRED)
			value = g_sock[pair].passcred[pipe_id_of_fd(fd) & 1];
		else
			return -ENOPROTOOPT;
	}
	else
		return -EBADF;
	len = 4;
	if (g_user->copy_to_user(optval, &value, 4) != 0 ||
	    g_user->copy_to_user(optlen, &len, 4) != 0)
		return -EFAULT;
	return 0;
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

int early_rootfs_fd_is_fifo(int fd)
{
	return node_of_fd(fd) == IR0_EARLY_ROOTFS_FIFO;
}

int64_t early_rootfs_fifo_write(int fd, uint64_t buffer, uint64_t count)
{
	int fsid = ramtab_body(pipe_id_of_fd(fd));
	int pipe_id;
	struct early_pipe *p;
	uint8_t tmp[256];
	unsigned room;
	unsigned put;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_FIFO)
		return -EBADF;
	if ((flags_of_fd(fd) & LINUX_O_ACCMODE) == 0)
		return -EBADF;
	if (fsid < 0 || fsid >= RAMTAB_MAX || !ramtab_table()[fsid].used)
		return -EBADF;
	pipe_id = ramtab_table()[fsid].fifo_pipe;
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
	g_sock[pair].shut[0] = 0;
	g_sock[pair].shut[1] = 0;
	g_sock[pair].reuseaddr = 0;
	g_sock[pair].so_error = 0;
	g_sock[pair].rights_n[0] = 0;
	g_sock[pair].rights_n[1] = 0;
	g_sock[pair].passcred[0] = 0;
	g_sock[pair].passcred[1] = 0;
	g_sock[pair].cred_valid[0] = 0;
	g_sock[pair].cred_valid[1] = 0;
	g_sock[pair].cred_explicit[0] = 0;
	g_sock[pair].cred_explicit[1] = 0;
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
	uint8_t hdr[2];

	if (uid < 0 || uid >= UNIX_MAX || !g_unix[uid].used || g_unix[uid].stream)
		return -ECONNREFUSED;
	if (count > 250)
		return -EMSGSIZE;
	room = PIPE_BUF - g_unix[uid].len;
	if (room < count + 2U)
		return -EAGAIN;
	hdr[0] = (uint8_t)(count & 0xffU);
	hdr[1] = (uint8_t)((count >> 8) & 0xffU);
	if (early_unix_deposit(&g_unix[uid], hdr, 2) != 2 ||
	    early_unix_deposit(&g_unix[uid], tmp, count) != count)
		return -EAGAIN;
	note_sender_creds_unix(uid);
	return (int64_t)count;
}

static int unix_find_bound(const char *path, int stream)
{
	int fsid;
	int i;

	fsid = ramtab_find(path);
	if (fsid < 0 || ramtab_node_kind(ramtab_table()[fsid].mode) != IR0_EARLY_ROOTFS_UNIX)
		return -1;
	fsid = ramtab_body(fsid);
	for (i = 0; i < UNIX_MAX; i++)
	{
		if (g_unix[i].used && ramtab_body(g_unix[i].bound_fs) == fsid &&
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

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -EBADF;
	if (uid >= 0)
	{
		if (!g_unix[uid].used)
			return -EBADF;
		if (g_unix[uid].stream)
			return -ENOTCONN;
		if (g_unix[uid].shut_wr)
			return -EPIPE;
		{
			int dest = g_unix[uid].peer_uid;

			if (dest < 0)
			{
				if (g_unix[uid].peer_fs < 0 ||
				    g_unix[uid].peer_fs >= RAMTAB_MAX ||
				    !ramtab_table()[g_unix[uid].peer_fs].used)
					return -ENOTCONN;
				dest = unix_find_bound(ramtab_table()[g_unix[uid].peer_fs].path,
						       0);
			}
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
	if (g_sock[pair].shut[end] & 2)
		return -EPIPE;
	if (g_sock[pair].refs[1 - end] == 0 || (g_sock[pair].shut[1 - end] & 1))
		return -EPIPE;
	if (count == 0)
		return 0;
	room = PIPE_BUF - g_sock[pair].dir[dst].len;
	if (g_sock[pair].dgram)
	{
		if (count > 250)
			return -EMSGSIZE;
		if (room < count + 2)
			return wait_or_eagain(fd);
		if (!g_user || !g_user->copy_from_user ||
		    g_user->copy_from_user(tmp, buffer, count) != 0)
			return -EFAULT;
		{
			uint8_t hdr[2];

			hdr[0] = (uint8_t)(count & 0xffU);
			hdr[1] = (uint8_t)((count >> 8) & 0xffU);
			if (early_sock_deposit(&g_sock[pair], dst, hdr, 2) != 2 ||
			    early_sock_deposit(&g_sock[pair], dst, tmp,
					       (unsigned)count) != (unsigned)count)
				return wait_or_eagain(fd);
		}
		note_sender_creds_sock(pair, dst);
		return (int64_t)count;
	}
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
	put = early_sock_deposit(&g_sock[pair], dst, tmp, put);
	note_sender_creds_sock(pair, dst);
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

static void copy_kstr(char *dst, unsigned cap, const char *src)
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

static int path_of_fd(int fd, char *out, unsigned cap)
{
	return path_of_node(node_of_fd(fd), pipe_id_of_fd(fd), out, cap);
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

static int landlock_allow(const char *path, uint64_t need)
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

static int landlock_open_need(const char *path, int flags)
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

static int parent_exists(const char *path)
{
	char parent[RAMTAB_NAME];

	if (!ramtab_parent(path, parent, sizeof(parent)))
		return 0;
	if (ir0_early_rootfs_path_node(parent) != IR0_EARLY_ROOTFS_NONE)
		return 1;
	return ramtab_find(parent) >= 0;
}

static int join_under_cwd(char *pbuf, unsigned cap)
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

static int fd_espipe(int fd)
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

static int64_t kread_at(int fd, uint64_t off, uint8_t *tmp, uint64_t count)
{
	enum ir0_early_rootfs_node node = node_of_fd(fd);
	uint64_t n;
	uint64_t i;
	uint64_t limit;

	if (!tmp || !count)
		return 0;
	if (node == IR0_EARLY_ROOTFS_NULL)
		return 0;
	if (node == IR0_EARLY_ROOTFS_ZERO)
	{
		n = count > 256 ? 256 : count;
		for (i = 0; i < n; i++)
			tmp[i] = 0;
		return (int64_t)n;
	}
	if (node == IR0_EARLY_ROOTFS_INIT)
	{
		if (off >= g_init_vis || !g_blob)
			return 0;
		limit = g_init_vis - off;
		if (off >= g_blob_len)
			return 0;
		if (limit > g_blob_len - off)
			limit = g_blob_len - off;
		n = count > limit ? limit : count;
		if (n > 256)
			n = 256;
		for (i = 0; i < n; i++)
			tmp[i] = g_blob[off + i];
		return (int64_t)n;
	}
	if (node == IR0_EARLY_ROOTFS_FILE)
	{
		int fsid = ramtab_body(pipe_id_of_fd(fd));

		if (fsid < 0 || fsid >= RAMTAB_MAX || !ramtab_table()[fsid].used)
			return -EBADF;
		if (off >= ramtab_table()[fsid].size)
			return 0;
		n = ramtab_table()[fsid].size - off;
		if (n > count)
			n = count;
		if (n > 256)
			n = 256;
		for (i = 0; i < n; i++)
			tmp[i] = ramtab_table()[fsid].data ? ramtab_table()[fsid].data[off + i] : 0;
		return (int64_t)n;
	}
	if (node == IR0_EARLY_ROOTFS_MEMFD)
	{
		int mid = pipe_id_of_fd(fd);

		if (mid < 0 || mid >= MEMFD_MAX || !g_memfd[mid].used)
			return -EBADF;
		if (off >= g_memfd[mid].len)
			return 0;
		n = g_memfd[mid].len - off;
		if (n > count)
			n = count;
		if (n > 256)
			n = 256;
		for (i = 0; i < n; i++)
			tmp[i] = g_memfd[mid].data[off + i];
		return (int64_t)n;
	}
	if (node == IR0_EARLY_ROOTFS_VDA)
	{
		uint8_t sector[512];
		uint64_t lba;
		uint64_t skip;

		if (!vda_present())
			return -EIO;
		if (off >= vda_bytes())
			return 0;
		lba = off / 512ULL;
		skip = off % 512ULL;
		if (ir0_block_read_by_name("vda", lba, 1, sector) != 0)
			return -EIO;
		n = 512ULL - skip;
		if (n > count)
			n = count;
		if (n > 256)
			n = 256;
		for (i = 0; i < n; i++)
			tmp[i] = sector[skip + i];
		return (int64_t)n;
	}
	return -ESPIPE;
}

static int64_t kwrite_buf(int fd, const uint8_t *tmp, uint64_t count)
{
	enum ir0_early_rootfs_node node = node_of_fd(fd);
	uint64_t *pos;
	uint64_t n;
	uint64_t i;
	uint64_t start;

	if (!tmp)
		return -EFAULT;
	if (count == 0)
		return 0;
	if (node == IR0_EARLY_ROOTFS_NULL || node == IR0_EARLY_ROOTFS_ZERO)
		return (int64_t)(count > 256 ? 256 : count);
	if (node == IR0_EARLY_ROOTFS_PIPE_W)
	{
		int pipe_id = pipe_id_of_fd(fd);
		struct early_pipe *p;
		unsigned room;
		unsigned put;

		if (pipe_id < 0)
			return -EBADF;
		p = &g_pipes[pipe_id];
		if (!p->used)
			return -EBADF;
		if (p->readers == 0)
			return -EPIPE;
		room = PIPE_BUF - p->len;
		if (room == 0)
			return wait_or_eagain(fd);
		put = room;
		if (put > 256)
			put = 256;
		if ((uint64_t)put > count)
			put = (unsigned)count;
		put = pipe_deposit(p, tmp, put);
		return (int64_t)put;
	}
	if (node == IR0_EARLY_ROOTFS_FILE)
	{
		int fsid = ramtab_body(pipe_id_of_fd(fd));

		pos = pos_of_fd(fd);
		if (fsid < 0 || fsid >= RAMTAB_MAX || !ramtab_table()[fsid].used || !pos)
			return -EBADF;
		if (!ramtab_table()[fsid].writable)
			return -EROFS;
		start = *pos;
		if (start >= RAMTAB_FILE_CAP)
			return -EFBIG;
		n = count;
		if (start + n > RAMTAB_FILE_CAP)
			n = RAMTAB_FILE_CAP - start;
		if (n > 256)
			n = 256;
		for (i = 0; i < n; i++)
			ramtab_table()[fsid].own[start + i] = tmp[i];
		*pos = start + n;
		if (*pos > ramtab_table()[fsid].size)
			ramtab_table()[fsid].size = (uint32_t)*pos;
		ramtab_table()[fsid].data = ramtab_table()[fsid].own;
		return (int64_t)n;
	}
	if (node == IR0_EARLY_ROOTFS_MEMFD)
	{
		int mid = pipe_id_of_fd(fd);

		pos = pos_of_fd(fd);
		if (mid < 0 || mid >= MEMFD_MAX || !g_memfd[mid].used || !pos)
			return -EBADF;
		start = *pos;
		if (start >= MEMFD_CAP)
			return -EINVAL;
		n = count;
		if (start + n > MEMFD_CAP)
			n = MEMFD_CAP - start;
		if (n > 256)
			n = 256;
		if (n == 0)
			return -EINVAL;
		for (i = 0; i < n; i++)
			g_memfd[mid].data[start + i] = tmp[i];
		if (start + n > g_memfd[mid].len)
			g_memfd[mid].len = (uint32_t)(start + n);
		*pos = start + n;
		return (int64_t)n;
	}
	return -EBADF;
}

int64_t early_rootfs_pread64(int fd, uint64_t buffer, uint64_t count,
			     int64_t offset)
{
	uint8_t tmp[256];
	int64_t n;
	int err;

	if (offset < 0)
		return -EINVAL;
	err = fd_espipe(fd);
	if (err < 0)
		return err;
	n = kread_at(fd, (uint64_t)offset, tmp, count);
	if (n <= 0)
		return n;
	if (!g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(buffer, tmp, (uint64_t)n) != 0)
		return -EFAULT;
	return n;
}

int64_t early_rootfs_pwrite64(int fd, uint64_t buffer, uint64_t count,
			      int64_t offset)
{
	uint64_t *pos;
	uint64_t saved;
	int64_t n;
	int err;

	if (offset < 0)
		return -EINVAL;
	err = fd_espipe(fd);
	if (err < 0)
		return err;
	pos = pos_of_fd(fd);
	if (!pos)
		return -EBADF;
	saved = *pos;
	*pos = (uint64_t)offset;
	if (node_of_fd(fd) == IR0_EARLY_ROOTFS_MEMFD)
		n = early_rootfs_memfd_write(fd, buffer, count);
	else if (node_of_fd(fd) == IR0_EARLY_ROOTFS_FILE)
		n = early_rootfs_file_write(fd, buffer, count);
	else if (node_of_fd(fd) == IR0_EARLY_ROOTFS_VDA)
		n = early_rootfs_block_write(fd, buffer, count);
	else if (node_of_fd(fd) == IR0_EARLY_ROOTFS_NULL ||
		 node_of_fd(fd) == IR0_EARLY_ROOTFS_ZERO)
	{
		uint8_t discard[256];

		n = (int64_t)(count > sizeof(discard) ? sizeof(discard) : count);
		if (n && (!g_user || !g_user->copy_from_user ||
			  g_user->copy_from_user(discard, buffer, (uint64_t)n) !=
				  0))
			n = -EFAULT;
	}
	else
		n = -EINVAL;
	*pos = saved;
	return n;
}

int64_t early_rootfs_sendfile(int out_fd, int in_fd, uint64_t offset,
			      uint64_t count)
{
	uint8_t tmp[256];
	uint64_t *inpos;
	int64_t off = 0;
	uint64_t done = 0;
	int err;

	err = fd_espipe(in_fd);
	if (err < 0)
		return err;
	if (node_of_fd(out_fd) == IR0_EARLY_ROOTFS_NONE)
		return -EBADF;
	if (offset)
	{
		if (!g_user || !g_user->copy_from_user ||
		    g_user->copy_from_user(&off, offset, sizeof(off)) != 0)
			return -EFAULT;
		if (off < 0)
			return -EINVAL;
	}
	else
	{
		inpos = pos_of_fd(in_fd);
		if (!inpos)
			return -EBADF;
		off = (int64_t)*inpos;
	}
	while (done < count)
	{
		int64_t n;
		int64_t w;

		n = kread_at(in_fd, (uint64_t)off, tmp, count - done);
		if (n < 0)
			return done ? (int64_t)done : n;
		if (n == 0)
			break;
		w = kwrite_buf(out_fd, tmp, (uint64_t)n);
		if (w < 0)
			return done ? (int64_t)done : w;
		off += w;
		done += (uint64_t)w;
		if (w < n)
			break;
	}
	if (offset)
	{
		if (!g_user || !g_user->copy_to_user ||
		    g_user->copy_to_user(offset, &off, sizeof(off)) != 0)
			return -EFAULT;
	}
	else
	{
		inpos = pos_of_fd(in_fd);
		if (inpos)
			*inpos = (uint64_t)off;
	}
	return (int64_t)done;
}

static int64_t kwrite_at(int fd, uint64_t off, const uint8_t *tmp, uint64_t n)
{
	uint64_t *pos = pos_of_fd(fd);
	uint64_t saved;
	int64_t w;

	if (!pos)
		return -EBADF;
	saved = *pos;
	*pos = off;
	w = kwrite_buf(fd, tmp, n);
	*pos = saved;
	return w;
}

int64_t early_rootfs_splice(int fd_in, uint64_t off_in, int fd_out,
			    uint64_t off_out, uint64_t len, unsigned flags)
{
	struct early_pipe *pin;
	struct early_pipe *pout;
	uint8_t tmp[256];
	uint64_t *inpos;
	uint64_t *outpos;
	int64_t in_off = 0;
	int64_t out_off = 0;
	unsigned n;
	int64_t got;
	int64_t put;
	int err;

	if (len == 0)
		return 0;
	if (!ir0_early_splice_flags_ok(flags))
		return -EINVAL;
	if (node_of_fd(fd_in) == IR0_EARLY_ROOTFS_NONE ||
	    node_of_fd(fd_out) == IR0_EARLY_ROOTFS_NONE)
		return -EBADF;
	pin = pipe_of_fd(fd_in);
	pout = pipe_of_fd(fd_out);
	if (!pin && !pout)
		return -EINVAL;
	if (pin && off_in)
		return -ESPIPE;
	if (pout && off_out)
		return -ESPIPE;
	if (pin && pout && pin == pout)
		return -EINVAL;
	if (!pin)
	{
		err = fd_espipe(fd_in);
		if (err < 0)
			return err;
		inpos = pos_of_fd(fd_in);
		err = load_loff(off_in, &in_off, inpos ? (int64_t)*inpos : 0);
		if (err < 0)
			return err;
	}
	if (!pout)
	{
		err = fd_espipe(fd_out);
		if (err < 0)
			return err;
		outpos = pos_of_fd(fd_out);
		err = load_loff(off_out, &out_off, outpos ? (int64_t)*outpos : 0);
		if (err < 0)
			return err;
	}
	n = len > 256 ? 256 : (unsigned)len;
	if (pin && pout)
	{
		if (pin->len == 0)
			return pin->writers == 0 ? 0 : splice_wait(fd_in, flags);
		if (pout->readers == 0)
			return -EPIPE;
		if (pout->len == PIPE_BUF)
			return splice_wait(fd_out, flags);
		n = pipe_consume(pin, tmp, n);
		n = pipe_deposit(pout, tmp, n);
		return (int64_t)n;
	}
	if (pin)
	{
		if (pin->len == 0)
			return pin->writers == 0 ? 0 : splice_wait(fd_in, flags);
		n = pipe_consume(pin, tmp, n);
		put = kwrite_at(fd_out, (uint64_t)out_off, tmp, n);
		if (put < 0)
			return put;
		out_off += put;
		if (off_out)
		{
			err = store_loff(off_out, out_off);
			if (err < 0)
				return err;
		}
		else
		{
			outpos = pos_of_fd(fd_out);
			if (outpos)
				*outpos = (uint64_t)out_off;
		}
		return put;
	}
	if (pout->readers == 0)
		return -EPIPE;
	if (pout->len == PIPE_BUF)
		return splice_wait(fd_out, flags);
	got = kread_at(fd_in, (uint64_t)in_off, tmp, n);
	if (got <= 0)
		return got;
	n = pipe_deposit(pout, tmp, (unsigned)got);
	in_off += n;
	if (off_in)
	{
		err = store_loff(off_in, in_off);
		if (err < 0)
			return err;
	}
	else
	{
		inpos = pos_of_fd(fd_in);
		if (inpos)
			*inpos = (uint64_t)in_off;
	}
	return (int64_t)n;
}

int64_t early_rootfs_tee(int fd_in, int fd_out, uint64_t len, unsigned flags)
{
	struct early_pipe *pin;
	struct early_pipe *pout;
	uint8_t tmp[256];
	unsigned n;

	if (len == 0)
		return 0;
	if (!ir0_early_splice_flags_ok(flags))
		return -EINVAL;
	pin = pipe_of_fd(fd_in);
	pout = pipe_of_fd(fd_out);
	if (!pin || !pout)
		return -EINVAL;
	if (pin == pout)
		return -EINVAL;
	if (pin->len == 0)
		return pin->writers == 0 ? 0 : splice_wait(fd_in, flags);
	if (pout->readers == 0)
		return -EPIPE;
	if (pout->len == PIPE_BUF)
		return splice_wait(fd_out, flags);
	n = len > 256 ? 256 : (unsigned)len;
	n = pipe_peek(pin, tmp, n);
	return (int64_t)pipe_deposit(pout, tmp, n);
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

int64_t early_rootfs_copy_file_range(int fd_in, uint64_t off_in, int fd_out,
				     uint64_t off_out, uint64_t len,
				     unsigned flags)
{
	uint8_t tmp[256];
	uint64_t *inpos;
	uint64_t *outpos;
	int64_t in_off;
	int64_t out_off;
	int64_t got;
	int64_t put;
	int err;

	if (flags)
		return -EINVAL;
	if (len == 0)
		return 0;
	err = fd_espipe(fd_in);
	if (err < 0)
		return err;
	err = fd_espipe(fd_out);
	if (err < 0)
		return err;
	inpos = pos_of_fd(fd_in);
	outpos = pos_of_fd(fd_out);
	err = load_loff(off_in, &in_off, inpos ? (int64_t)*inpos : 0);
	if (err < 0)
		return err;
	err = load_loff(off_out, &out_off, outpos ? (int64_t)*outpos : 0);
	if (err < 0)
		return err;
	if (fd_in == fd_out ||
	    (node_of_fd(fd_in) == node_of_fd(fd_out) &&
	     pipe_id_of_fd(fd_in) == pipe_id_of_fd(fd_out)))
	{
		int64_t in_end = in_off + (int64_t)len;
		int64_t out_end = out_off + (int64_t)len;

		if (in_off < out_end && out_off < in_end)
			return -EINVAL;
	}
	got = kread_at(fd_in, (uint64_t)in_off, tmp, len > 256 ? 256 : len);
	if (got <= 0)
		return got;
	put = kwrite_at(fd_out, (uint64_t)out_off, tmp, (uint64_t)got);
	if (put < 0)
		return put;
	in_off += put;
	out_off += put;
	if (off_in)
	{
		err = store_loff(off_in, in_off);
		if (err < 0)
			return err;
	}
	else if (inpos)
		*inpos = (uint64_t)in_off;
	if (off_out)
	{
		err = store_loff(off_out, out_off);
		if (err < 0)
			return err;
	}
	else if (outpos)
		*outpos = (uint64_t)out_off;
	return put;
}

int64_t early_rootfs_fadvise64(int fd, int64_t offset, uint64_t len, int advice)
{
	int err;

	(void)len;
	err = fd_espipe(fd);
	if (err < 0)
		return err;
	if (offset < 0 || !ir0_early_fadvise_ok(advice))
		return -EINVAL;
	return 0;
}

int64_t early_rootfs_vmsplice(int fd, uint64_t iov, uint64_t iovcnt,
			      unsigned flags)
{
	struct early_pipe *p = pipe_of_fd(fd);
	enum ir0_early_rootfs_node node = node_of_fd(fd);
	int acc = flags_of_fd(fd) & LINUX_O_ACCMODE;
	uint8_t tmp[256];
	uint64_t i;
	int64_t total = 0;

	if (!p)
		return node == IR0_EARLY_ROOTFS_NONE ? -EBADF : -EINVAL;
	if (!ir0_early_splice_flags_ok(flags))
		return -EINVAL;
	if (iovcnt > 8)
		return -EINVAL;
	for (i = 0; i < iovcnt; i++)
	{
		uint64_t vec[2];
		unsigned n;

		if (!g_user || !g_user->copy_from_user ||
		    g_user->copy_from_user(vec, iov + i * 16UL, sizeof(vec)) != 0)
			return total ? total : -EFAULT;
		if (vec[1] == 0)
			continue;
		n = vec[1] > 256 ? 256 : (unsigned)vec[1];
		if (node == IR0_EARLY_ROOTFS_PIPE_W ||
		    (node == IR0_EARLY_ROOTFS_FIFO && acc != 0))
		{
			if (p->readers == 0)
				return total ? total : -EPIPE;
			if (p->len == PIPE_BUF)
				return total ? total : splice_wait(fd, flags);
			if (g_user->copy_from_user(tmp, vec[0], n) != 0)
				return total ? total : -EFAULT;
			n = pipe_deposit(p, tmp, n);
		}
		else
		{
			if (p->len == 0)
				return total ? total :
					       (p->writers == 0 ?
							0 :
							splice_wait(fd, flags));
			n = pipe_consume(p, tmp, n);
			if (!g_user->copy_to_user ||
			    g_user->copy_to_user(vec[0], tmp, n) != 0)
				return total ? total : -EFAULT;
		}
		total += n;
		if ((uint64_t)n < vec[1])
			break;
	}
	return total;
}

int64_t early_rootfs_sync_file_range(int fd, int64_t offset, int64_t nbytes,
				     unsigned flags)
{
	int err;

	if (offset < 0 || nbytes < 0)
		return -EINVAL;
	if (!ir0_early_sync_file_range_flags_ok(flags))
		return -EINVAL;
	err = fd_espipe(fd);
	if (err < 0)
		return err;
	return 0;
}

int64_t early_rootfs_readahead(int fd, int64_t offset, uint64_t count)
{
	int err;

	(void)count;
	err = fd_espipe(fd);
	if (err < 0)
		return err == -ESPIPE ? -EINVAL : err;
	if (offset < 0)
		return -EINVAL;
	return 0;
}

int64_t early_rootfs_syncfs(int fd)
{
	if (node_of_fd(fd) == IR0_EARLY_ROOTFS_NONE)
		return -EBADF;
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

int64_t early_rootfs_fallocate(int fd, int mode, int64_t offset, int64_t len)
{
	uint64_t end;
	int err;

	if (offset < 0 || len <= 0)
		return -EINVAL;
	err = fd_espipe(fd);
	if (err < 0)
		return err;
	if (mode & ~IR0_EARLY_FALLOC_KEEP_SIZE)
		return -EOPNOTSUPP;
	end = (uint64_t)offset + (uint64_t)len;
	if (end < (uint64_t)offset)
		return -EFBIG;
	if (mode & IR0_EARLY_FALLOC_KEEP_SIZE)
		return 0;
	return early_rootfs_ftruncate(fd, end);
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
