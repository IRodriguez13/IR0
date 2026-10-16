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
#define AT_REMOVEDIR 0x200
#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_NB 4
#define LOCK_UN 8

#define ROOTFS_MAX_OPEN 8U
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

		if (pipe_id >= 0 && pipe_id < PIPE_MAX && g_pipes[pipe_id].used)
		{
			if (g_slots[idx].node == IR0_EARLY_ROOTFS_PIPE_R &&
			    g_pipes[pipe_id].readers > 0)
				g_pipes[pipe_id].readers--;
			if (g_slots[idx].node == IR0_EARLY_ROOTFS_PIPE_W &&
			    g_pipes[pipe_id].writers > 0)
				g_pipes[pipe_id].writers--;
			if (g_pipes[pipe_id].readers == 0 &&
			    g_pipes[pipe_id].writers == 0)
				g_pipes[pipe_id].used = 0;
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
	       node == IR0_EARLY_ROOTFS_TMP;
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
	apply_chroot(pbuf, 32);
	return ir0_early_rootfs_path_node(pbuf);
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
	if ((flags & LINUX_O_DIRECTORY) && !node_is_dir(node))
		return -ENOTDIR;
	fd = alloc_fd(node);
	if (fd < 0)
		return fd;
	set_flags_of_fd(fd, status_flags(flags));
	set_cloexec_of_fd(fd, (flags & LINUX_O_CLOEXEC) != 0);
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
	{
		int pipe_id = pipe_id_of_fd(oldfd);
		int nidx = slot_from_fd(newfd);

		if (nidx >= 0)
			g_slots[nidx].pipe_id = pipe_id;
		if (pipe_id >= 0 && pipe_id < PIPE_MAX && g_pipes[pipe_id].used)
		{
			if (node_of_fd(oldfd) == IR0_EARLY_ROOTFS_PIPE_R)
				g_pipes[pipe_id].readers++;
			if (node_of_fd(oldfd) == IR0_EARLY_ROOTFS_PIPE_W)
				g_pipes[pipe_id].writers++;
		}
	}
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
	{
		int pipe_id = pipe_id_of_fd(oldfd);
		int nidx = slot_from_fd(newfd);

		if (nidx >= 0)
			g_slots[nidx].pipe_id = pipe_id;
		if (pipe_id >= 0 && pipe_id < PIPE_MAX && g_pipes[pipe_id].used)
		{
			if (node_of_fd(oldfd) == IR0_EARLY_ROOTFS_PIPE_R)
				g_pipes[pipe_id].readers++;
			if (node_of_fd(oldfd) == IR0_EARLY_ROOTFS_PIPE_W)
				g_pipes[pipe_id].writers++;
		}
	}
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
	ir0_early_rootfs_fill_stat(node,
				   node == IR0_EARLY_ROOTFS_VDA ? vda_bytes() :
				   node == IR0_EARLY_ROOTFS_INIT ? g_init_vis :
								  g_blob_len,
				   &st);
	if (!g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(statbuf, &st, sizeof(st)) != 0)
		return -EFAULT;
	return 0;
}

int64_t early_rootfs_fstat(int fd, uint64_t statbuf)
{
	enum ir0_early_rootfs_node node;
	struct ir0_early_stat64 st;

	node = node_of_fd(fd);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -EBADF;
	ir0_early_rootfs_fill_stat(node,
				   node == IR0_EARLY_ROOTFS_VDA ? vda_bytes() :
				   node == IR0_EARLY_ROOTFS_INIT ? g_init_vis :
								  g_blob_len,
				   &st);
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
	total = ir0_early_rootfs_dir_count(node);
	if (node == IR0_EARLY_ROOTFS_DEV && vda_present())
		total++;
	if (total == 0)
		return -ENOTDIR;
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

		if (g_slots[idx].pos < ir0_early_rootfs_dir_count(node))
			spec = ir0_early_rootfs_dir_entry(node,
							 (unsigned)g_slots[idx].pos);
		else
		{
			spec.name = "vda";
			spec.ino = 8;
			spec.type = IR0_EARLY_DT_BLK;
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
	    node == IR0_EARLY_ROOTFS_PIPE_R || node == IR0_EARLY_ROOTFS_PIPE_W)
		return -ESPIPE;
	pos = pos_of_fd(fd);
	if (!pos)
		return -EBADF;
	if (node == IR0_EARLY_ROOTFS_INIT)
		limit = g_init_vis;
	else if (node == IR0_EARLY_ROOTFS_VDA)
		limit = vda_bytes();
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
	if (node == IR0_EARLY_ROOTFS_CONSOLE || node == IR0_EARLY_ROOTFS_TTY)
		ready = IR0_EARLY_POLLOUT;
	else if (node == IR0_EARLY_ROOTFS_NULL || node == IR0_EARLY_ROOTFS_ZERO ||
		 node == IR0_EARLY_ROOTFS_VDA || node == IR0_EARLY_ROOTFS_INIT)
		ready = IR0_EARLY_POLLIN | IR0_EARLY_POLLOUT;
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
	if (node == IR0_EARLY_ROOTFS_PIPE_R || node == IR0_EARLY_ROOTFS_PIPE_W)
		return -ESPIPE;
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
	char pbuf[32];
	enum ir0_early_rootfs_node node;

	if (!g_ready)
		return -ENOENT;
	if (dirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(dirfd) < 0)
		return -EBADF;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;
	node = lookup_user_path(pbuf);
	if (node != IR0_EARLY_ROOTFS_NONE)
		return -EEXIST;
	return -EROFS;
}

int64_t early_rootfs_unlinkat(int dirfd, uint64_t path, int flags)
{
	char pbuf[32];
	enum ir0_early_rootfs_node node;

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
	if (node_is_dir(node))
	{
		if (!(flags & AT_REMOVEDIR))
			return -EISDIR;
		return -EBUSY;
	}
	if (flags & AT_REMOVEDIR)
		return -ENOTDIR;
	return -EPERM;
}

int64_t early_rootfs_renameat(int olddirfd, uint64_t oldpath, int newdirfd,
			      uint64_t newpath)
{
	char oldbuf[32];
	enum ir0_early_rootfs_node node;

	(void)newdirfd;
	(void)newpath;
	if (!g_ready)
		return -ENOENT;
	if (olddirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(olddirfd) < 0)
		return -EBADF;
	if (!copy_user_cstr(oldpath, oldbuf, sizeof(oldbuf)))
		return -EFAULT;
	node = lookup_user_path(oldbuf);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -ENOENT;
	return -EACCES;
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
