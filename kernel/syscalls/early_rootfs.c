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
#include <ir0/early_rootfs.h>

#define EBADF  9
#define EFAULT 14
#define EINVAL 22
#define EISDIR 21
#define ENOENT 2
#define ENOTDIR 20
#define EMFILE 24
#define ERANGE 34

#define ROOTFS_MAX_OPEN 8U
#define ROOTFS_FD_BASE  3
#define ROOTFS_FD_LIMIT ((int)(ROOTFS_FD_BASE + ROOTFS_MAX_OPEN))
#define LINUX_O_CLOEXEC 02000000

struct rootfs_slot
{
	int used;
	enum ir0_early_rootfs_node node;
	uint64_t pos;
};

static struct rootfs_slot g_slots[ROOTFS_MAX_OPEN];
static const uint8_t *g_blob;
static uint64_t g_blob_len;
static const struct early_user_services *g_user;
static int g_ready;
static int g_stdio_open[3];
static enum ir0_early_rootfs_node g_stdio_node[3];
static enum ir0_early_rootfs_node g_cwd;

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
	g_cwd = IR0_EARLY_ROOTFS_ROOT;
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

static int close_fd(int fd)
{
	int idx;

	if (fd >= 0 && fd <= 2)
	{
		if (!g_stdio_open[fd])
			return -EBADF;
		g_stdio_open[fd] = 0;
		g_stdio_node[fd] = IR0_EARLY_ROOTFS_NONE;
		return 0;
	}
	idx = slot_from_fd(fd);
	if (idx < 0)
		return -EBADF;
	g_slots[idx].used = 0;
	g_slots[idx].node = IR0_EARLY_ROOTFS_NONE;
	g_slots[idx].pos = 0;
	return 0;
}

static int install_fd(int fd, enum ir0_early_rootfs_node node)
{
	unsigned idx;

	if (fd >= 0 && fd <= 2)
	{
		g_stdio_open[fd] = 1;
		g_stdio_node[fd] = node;
		return fd;
	}
	if (fd < ROOTFS_FD_BASE || fd >= ROOTFS_FD_LIMIT)
		return -EBADF;
	idx = (unsigned)(fd - ROOTFS_FD_BASE);
	g_slots[idx].used = 1;
	g_slots[idx].node = node;
	g_slots[idx].pos = 0;
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
	g_user = user;
	for (i = 0; i < ROOTFS_MAX_OPEN; i++)
	{
		g_slots[i].used = 0;
		g_slots[i].node = IR0_EARLY_ROOTFS_NONE;
		g_slots[i].pos = 0;
	}
	reset_stdio();
	g_ready = (init_blob != 0 && init_len > 0 && user &&
		   user->copy_from_user && user->copy_to_user);
}

int early_rootfs_ready(void)
{
	return g_ready;
}

int64_t early_rootfs_openat(int dirfd, uint64_t path, int flags)
{
	char pbuf[32];
	enum ir0_early_rootfs_node node;
	int fd;

	(void)flags;
	if (!g_ready)
		return -ENOENT;
	if (dirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(dirfd) < 0)
		return -EBADF;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;
	node = ir0_early_rootfs_path_node(pbuf);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -ENOENT;
	fd = alloc_fd(node);
	if (fd < 0)
		return fd;
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
	if (node == IR0_EARLY_ROOTFS_ROOT || node == IR0_EARLY_ROOTFS_DEV)
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
	idx = slot_from_fd(fd);
	if (idx < 0)
		return -EBADF;
	sl = &g_slots[idx];
	if (!g_blob || sl->pos >= g_blob_len)
		return 0;
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
	return install_fd(newfd, node_of_fd(oldfd));
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
	return install_fd(newfd, node_of_fd(oldfd));
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
		return -ENOENT;
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

	(void)flags;
	if (!g_ready)
		return -ENOENT;
	if (dirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(dirfd) < 0)
		return -EBADF;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;
	if (ir0_early_rootfs_path_node(pbuf) == IR0_EARLY_ROOTFS_NONE)
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
	node = ir0_early_rootfs_path_node(pbuf);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -ENOENT;
	ir0_early_rootfs_fill_stat(node, g_blob_len, &st);
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
	ir0_early_rootfs_fill_stat(node, g_blob_len, &st);
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
	node = ir0_early_rootfs_path_node(pbuf);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -ENOENT;
	if (node != IR0_EARLY_ROOTFS_ROOT && node != IR0_EARLY_ROOTFS_DEV)
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

		spec = ir0_early_rootfs_dir_entry(node, (unsigned)g_slots[idx].pos);
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
	if (n != 4 || ir0_early_rootfs_dir_count(IR0_EARLY_ROOTFS_DEV) != 6)
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
	cwd = (g_cwd == IR0_EARLY_ROOTFS_DEV) ? "/dev" : "/";
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
