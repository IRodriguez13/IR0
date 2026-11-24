/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_heartfs_vfs.c
 * Description: Host tests for the heartfs vfs_fstype: mount, lookup (stat),
 *              readdir, read, open and the /heart/proc|sys views.
 */

#include "test_harness_ir0.h"
#include <fs/vfs.h>
#include <ir0/cred.h>
#include <ir0/errno.h>
#include <ir0/open_flags.h>
#include <ir0/vfs_file.h>
#include <ir0/stat.h>
#include <ir0/statfs.h>
#include <string.h>

extern struct vfs_fstype heartfs_fs_type;

static char g_bound_source[64];
static char g_bound_target[64];

int vfs_bind_mount(const char *source, const char *target)
{
	strncpy(g_bound_source, source, sizeof(g_bound_source) - 1);
	strncpy(g_bound_target, target, sizeof(g_bound_target) - 1);
	return 0;
}

const struct ir0_task_cred *ir0_current_cred(void)
{
	return NULL;
}

/*
 * A two-file VFS behind the aliases: heartfs reaches /proc and /sys only
 * through vfs_stat/vfs_readdir/vfs_open/vfs_pread, whoever serves them.
 */
static const char g_uptime[] = "12.50 3.00\n";
static struct vfs_file g_file;

int vfs_stat(const char *path, stat_t *st)
{
	memset(st, 0, sizeof(*st));
	if (strcmp(path, "/proc") == 0 || strcmp(path, "/sys") == 0)
	{
		st->st_mode = S_IFDIR | 0555;
		return 0;
	}
	if (strcmp(path, "/proc/uptime") == 0)
	{
		st->st_mode = S_IFREG | 0444;
		st->st_size = (off_t)strlen(g_uptime);
		return 0;
	}
	if (strcmp(path, "/sys/kernel/version") == 0)
	{
		st->st_mode = S_IFREG | 0444;
		return 0;
	}
	return -ENOENT;
}

int vfs_readdir(const char *path, struct vfs_dirent *entries, int max)
{
	const char *name = strcmp(path, "/proc") == 0 ? "uptime" : "kernel";

	if (max < 1)
		return 0;
	strncpy(entries[0].name, name, sizeof(entries[0].name) - 1);
	entries[0].type = strcmp(path, "/proc") == 0 ? DT_REG : DT_DIR;
	return 1;
}

int vfs_open(const char *path, int flags, mode_t mode, struct vfs_file **out)
{
	stat_t st;
	int ret = vfs_stat(path, &st);

	(void)mode;
	if (ret != 0)
		return ret;
	if ((flags & IR0_O_ACCMODE) != IR0_O_RDONLY)
		return -EACCES;
	strncpy(g_file.path, path, sizeof(g_file.path) - 1);
	*out = &g_file;
	return 0;
}

int vfs_pread(struct vfs_file *f, char *buf, size_t count, off_t offset)
{
	size_t len = strlen(g_uptime);

	if (strcmp(f->path, "/proc/uptime") != 0 || (size_t)offset >= len)
		return 0;
	len -= (size_t)offset;
	if (len > count)
		len = count;
	memcpy(buf, g_uptime + offset, len);
	return (int)len;
}

int vfs_pwrite(struct vfs_file *f, const char *buf, size_t count, off_t offset)
{
	(void)f;
	(void)buf;
	(void)count;
	(void)offset;
	return -EINVAL;
}

int vfs_close(struct vfs_file *f)
{
	(void)f;
	return 0;
}

static int find_dirent(const struct vfs_dirent *ents, int n, const char *name)
{
	for (int i = 0; i < n; i++)
	{
		if (strcmp(ents[i].name, name) == 0)
			return i;
	}
	return -1;
}

static int read_at(const char *path, char *buf, size_t count, off_t off,
		   size_t *done)
{
	return heartfs_fs_type.ops->read(path, buf, count, done, off);
}

void test_heartfs_vfs(void)
{
	static const char *const root_names[] = {
		"README", "MANIFESTO", "proc", "sys", "kernel", "src", "dennis"
	};
	static const unsigned char root_types[] = {
		DT_REG, DT_REG, DT_DIR, DT_DIR, DT_DIR, DT_DIR, DT_DIR
	};
	struct vfs_ops *ops = heartfs_fs_type.ops;
	struct vfs_dirent ents[16];
	struct ir0_statfs sfs;
	char buf[64];
	stat_t st;
	size_t done;
	int n;

	TEST_BEGIN("heartfs_vfs");

	ASSERT_STR_EQ(heartfs_fs_type.name, "heartfs");
	ASSERT(heartfs_fs_type.fs_flags & FS_NOSUID_DEFAULT);
	ASSERT_EQ(heartfs_fs_type.mount("heartfs", "/mnt"), -EINVAL);
	ASSERT_EQ(heartfs_fs_type.mount("heartfs", "/heart"), 0);
	ASSERT_STR_EQ(g_bound_source, "/heart/dennis");
	ASSERT_STR_EQ(g_bound_target, "/heart/dennis");
	ASSERT_EQ(heartfs_fs_type.umount("/heart"), -EBUSY);
	memset(&sfs, 0, sizeof(sfs));
	ASSERT_EQ(heartfs_fs_type.statfs("/heart", &sfs), 0);
	ASSERT_EQ(sfs.f_type, IR0_HEARTFS_MAGIC);
	ASSERT_EQ(sfs.f_blocks, 0U);

	/* Lookup. */
	ASSERT_EQ(ops->stat("/heart", &st), 0);
	ASSERT_EQ(st.st_mode, (mode_t)(S_IFDIR | 0555));
	ASSERT(st.st_nlink == 2);
	ASSERT_EQ(ops->stat("/heart/", &st), 0);
	ASSERT(S_ISDIR(st.st_mode));
	ASSERT_EQ(ops->stat("/heart/kernel", &st), 0);
	ASSERT(S_ISDIR(st.st_mode));
	ASSERT_EQ(ops->stat("/heart/src/includes/ir0", &st), 0);
	ASSERT(S_ISDIR(st.st_mode));
	ASSERT_EQ(ops->stat("/heart/README", &st), 0);
	ASSERT_EQ(st.st_mode, (mode_t)(S_IFREG | 0444));
	ASSERT(st.st_nlink == 1);
	ASSERT(st.st_size > 8);
	ASSERT_EQ(ops->stat("/heart/kernel/version", &st), 0);
	ASSERT(S_ISREG(st.st_mode) && st.st_size > 1);
	ASSERT_EQ(ops->stat("/heart/src/includes/ir0/heart_src_probe.h", &st), 0);
	ASSERT(S_ISREG(st.st_mode));
	ASSERT_EQ(ops->stat("/heart/nope", &st), -ENOENT);
	ASSERT_EQ(ops->stat("/heart/kernel/nope", &st), -ENOENT);
	ASSERT_EQ(ops->stat("/heartx", &st), -ENOENT);

	/* Readdir. */
	n = ops->readdir("/heart", ents, 16);
	ASSERT_EQ(n, 7);
	for (int i = 0; i < 7; i++)
	{
		ASSERT_STR_EQ(ents[i].name, root_names[i]);
		ASSERT_EQ(ents[i].type, root_types[i]);
	}
	n = ops->readdir("/heart/kernel", ents, 16);
	ASSERT_EQ(n, 3);
	ASSERT_STR_EQ(ents[0].name, "version");
	ASSERT_STR_EQ(ents[1].name, "build");
	ASSERT_STR_EQ(ents[2].name, "features");
	ASSERT_EQ(ents[2].type, DT_REG);
	n = ops->readdir("/heart/src", ents, 16);
	ASSERT(n > 0);
	ASSERT(find_dirent(ents, n, "includes") >= 0);
	ASSERT_EQ(ents[find_dirent(ents, n, "includes")].type, DT_DIR);
	n = ops->readdir("/heart/src/includes/ir0", ents, 16);
	ASSERT(find_dirent(ents, n, "heart_src_probe.h") >= 0);
	ASSERT_EQ(ents[find_dirent(ents, n, "heart_src_probe.h")].type, DT_REG);
	ASSERT_EQ(ops->readdir("/heart/dennis", ents, 16), 0);
	ASSERT_EQ(ops->readdir("/heart/README", ents, 16), -ENOTDIR);
	ASSERT_EQ(ops->readdir("/heart", ents, 2), 2);

	/* Read: slices at the offset, 0 at EOF, EISDIR on a directory. */
	ASSERT_EQ(ops->stat("/heart/README", &st), 0);
	memset(buf, 0, sizeof(buf));
	ASSERT_EQ(read_at("/heart/README", buf, 4, 0, &done), 0);
	ASSERT_EQ(done, (size_t)4);
	ASSERT_STR_EQ(buf, "IR0 ");
	ASSERT_EQ(read_at("/heart/README", buf, 4, st.st_size - 2, &done), 0);
	ASSERT_EQ(done, (size_t)2);
	ASSERT_EQ(read_at("/heart/README", buf, 4, st.st_size, &done), 0);
	ASSERT_EQ(done, (size_t)0);
	ASSERT_EQ(read_at("/heart/README", buf, 4, -1, &done), -EINVAL);
	ASSERT_EQ(read_at("/heart/kernel", buf, 4, 0, &done), -EISDIR);
	ASSERT_EQ(read_at("/heart/nope", buf, 4, 0, &done), -ENOENT);
	ASSERT_EQ(ops->stat("/heart/src/kernel/entry/syscall_dispatch.c", &st), 0);
	ASSERT(st.st_size > 4096);
	ASSERT_EQ(read_at("/heart/src/kernel/entry/syscall_dispatch.c", buf,
			  sizeof(buf), st.st_size - 1, &done), 0);
	ASSERT_EQ(done, (size_t)1);
	ASSERT_EQ(buf[0], '\n');

	/* Own files take no writes; O_TRUNC is accepted. */
	ASSERT_EQ(ops->write("/heart/README", "x", 1, &done, 0), -EINVAL);
	ASSERT_EQ(ops->truncate("/heart/README", 0), 0);
	ASSERT_EQ(ops->truncate("/heart/kernel", 0), -EISDIR);
	ASSERT_EQ(ops->create("/heart/new", 0644), -EACCES);
	ASSERT_EQ(ops->open("/heart/README", 0), 0);

	/* /heart/proc and /heart/sys view /proc and /sys through the VFS. */
	ASSERT_EQ(ops->stat("/heart/proc", &st), 0);
	ASSERT(S_ISDIR(st.st_mode));
	n = ops->readdir("/heart/proc", ents, 16);
	ASSERT_EQ(n, 1);
	ASSERT_STR_EQ(ents[0].name, "uptime");
	ASSERT_EQ(ents[0].type, DT_REG);
	ASSERT_EQ(ops->stat("/heart/proc/uptime", &st), 0);
	ASSERT_EQ(st.st_size, 11);
	ASSERT_EQ(ops->open("/heart/proc/uptime", 0), 0);
	ASSERT_EQ(ops->open("/heart/proc/missing", 0), -ENOENT);
	memset(buf, 0, sizeof(buf));
	ASSERT_EQ(read_at("/heart/proc/uptime", buf, sizeof(buf) - 1, 6, &done), 0);
	ASSERT_EQ(done, (size_t)5);
	ASSERT_STR_EQ(buf, "3.00\n");
	ASSERT_EQ(ops->write("/heart/proc/uptime", "1", 1, &done, 0), -EACCES);
	n = ops->readdir("/heart/sys", ents, 16);
	ASSERT(find_dirent(ents, n, "kernel") >= 0);
	ASSERT_EQ(ents[find_dirent(ents, n, "kernel")].type, DT_DIR);
	ASSERT_EQ(ops->stat("/heart/sys/kernel/version", &st), 0);
	ASSERT_EQ(st.st_mode, (mode_t)(S_IFREG | 0444));

	TEST_END();
}
