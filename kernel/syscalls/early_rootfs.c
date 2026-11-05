/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel/syscalls/early_rootfs.c
 * Description: staged root, descriptor table and core early VFS operations.
 */

#include "early_rootfs_internal.h"

static void early_publish_fd(int fd, enum ir0_early_rootfs_node node);
static int early_fdtable_ok(int fd);
static struct vfs_ops g_early_vfs_ops;

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

int product_path_live(void)
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

struct vfs_file *vf_of_fd(int fd)
{
	int idx = slot_from_fd(fd);

	if (idx < 0)
		return 0;
	return g_slots[idx].vf;
}

int64_t product_xfer(int fd, uint64_t buffer, uint64_t count, int write)
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
	node = product_lookup(pbuf);
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

void early_publish_root(void)
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

static struct vfs_ops g_early_vfs_ops = {
	.stat = early_vfs_ops_stat,
	.read = early_vfs_ops_read,
};
