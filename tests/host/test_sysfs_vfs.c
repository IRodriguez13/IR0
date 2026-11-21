/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_sysfs_vfs.c
 * Description: Host tests for the sysfs vfs_fstype: mount, lookup (stat),
 *              readdir, read, write and open.
 */

#include "test_harness_ir0.h"
#include <fs/vfs.h>
#include <ir0/errno.h>
#include <ir0/open_flags.h>
#include <ir0/stat.h>
#include <ir0/statfs.h>
#include <ir0/sysfs.h>
#include <fs/sysfs_internal.h>
#include <string.h>

extern struct vfs_fstype sysfs_fs_type;

/* Same wrappers as fs/sysfs.c. The host suite does not link that file. */
int sysfs_alias_stat(const char *path, stat_t *st)
{
	return sysfs_exact_stat(path, st);
}

int sysfs_alias_read(const char *path, void *buf, size_t count,
		     size_t *bytes_read, off_t offset)
{
	return sysfs_exact_read(path, buf, count, bytes_read, offset);
}

int sysfs_alias_write(const char *path, const void *buf, size_t count,
		      size_t *bytes_written, off_t offset)
{
	return sysfs_exact_write(path, buf, count, bytes_written, offset);
}

int sysfs_alias_open(const char *path, int flags)
{
	return sysfs_exact_open(path, flags);
}

int sysfs_alias_readdir(const char *path, struct vfs_dirent *entries, int max)
{
	return sysfs_fs_type.ops->readdir(path, entries, max);
}

static int read_at(const char *path, char *buf, size_t count, off_t off,
		   size_t *done)
{
	return sysfs_fs_type.ops->read(path, buf, count, done, off);
}

void test_sysfs_vfs(void)
{
	static const char *const root_names[] = {
		"kernel", "firmware", "devices", "console", "class"
	};
	struct vfs_ops *ops = sysfs_fs_type.ops;
	struct vfs_dirent ents[16];
	struct ir0_statfs sfs;
	char buf[32];
	stat_t st;
	size_t done;
	int n;
	int i;

	TEST_BEGIN("sysfs_vfs");

	ASSERT_STR_EQ(sysfs_fs_type.name, "sysfs");
	ASSERT(sysfs_fs_type.fs_flags & FS_NOSUID_DEFAULT);
	ASSERT_EQ(sysfs_fs_type.mount("sysfs", "/mnt"), -EINVAL);
	ASSERT_EQ(sysfs_fs_type.mount("sysfs", "/sys"), 0);
	ASSERT_EQ(sysfs_fs_type.umount("/sys"), -EBUSY);
	memset(&sfs, 0, sizeof(sfs));
	ASSERT_EQ(sysfs_fs_type.statfs("/sys", &sfs), 0);
	ASSERT_EQ(sfs.f_type, IR0_SYSFS_MAGIC);
	ASSERT_EQ(sfs.f_blocks, 0UL);

	/* Lookup: directories synthesized, exact files keep their mode. */
	ASSERT_EQ(ops->stat("/sys", &st), 0);
	ASSERT_EQ(st.st_mode, (mode_t)(S_IFDIR | 0555));
	ASSERT(st.st_nlink == 2);
	ASSERT_EQ(ops->stat("/sys/", &st), 0);
	ASSERT(S_ISDIR(st.st_mode));
	ASSERT_EQ(ops->stat("/sys/kernel", &st), 0);
	ASSERT(S_ISDIR(st.st_mode));
	ASSERT_EQ(ops->stat("/sys/kernel/", &st), 0);
	ASSERT(S_ISDIR(st.st_mode));
	ASSERT_EQ(ops->stat("/sys/firmware/ir0", &st), 0);
	ASSERT(S_ISDIR(st.st_mode));
	ASSERT_EQ(ops->stat("/sys/kernel/hostname", &st), 0);
	ASSERT_EQ(st.st_mode, (mode_t)(S_IFREG | 0664));
	ASSERT_EQ(st.st_size, (off_t)4096);
	ASSERT_EQ(ops->stat("/sys/kernel/version", &st), 0);
	ASSERT_EQ(st.st_mode, (mode_t)(S_IFREG | 0444));
	ASSERT_EQ(st.st_size, (off_t)0);
	ASSERT_EQ(ops->stat("/sys/kernel/panic", &st), 0);
	ASSERT_EQ(st.st_mode, (mode_t)(S_IFREG | 0644));
	ASSERT_EQ(ops->stat("/sys/devices/system", &st), 0);
	ASSERT(S_ISREG(st.st_mode));
	ASSERT_EQ(ops->stat("/sys/devices/system/cpu/cpu0/online", &st), 0);
	ASSERT_EQ(st.st_mode, (mode_t)(S_IFREG | 0664));
	ASSERT_EQ(ops->stat("/sys/devices/system/cpu/cpu1", &st), 0);
	ASSERT(S_ISREG(st.st_mode));
	ASSERT_EQ(ops->stat("/sys/devices/system/cpu/cpu2", &st), -ENOENT);
	ASSERT_EQ(ops->stat("/sys/nope", &st), -ENOENT);
	ASSERT_EQ(ops->stat("/sys/kernel/hostname/extra", &st), -ENOENT);
	ASSERT_EQ(ops->stat("/sysx", &st), -ENOENT);

	/* Readdir follows registration order; the first name wins its type. */
	n = ops->readdir("/sys", ents, 16);
	ASSERT_EQ(n, 5);
	for (i = 0; i < 5; i++)
	{
		ASSERT_STR_EQ(ents[i].name, root_names[i]);
		ASSERT_EQ(ents[i].type, DT_DIR);
	}
	n = ops->readdir("/sys/kernel", ents, 16);
	ASSERT_EQ(n, 11);
	ASSERT_STR_EQ(ents[0].name, "hostname");
	ASSERT_EQ(ents[0].type, DT_REG);
	ASSERT_STR_EQ(ents[1].name, "version");
	ASSERT_STR_EQ(ents[10].name, "halt");
	n = ops->readdir("/sys/firmware", ents, 16);
	ASSERT_EQ(n, 1);
	ASSERT_STR_EQ(ents[0].name, "ir0");
	ASSERT_EQ(ents[0].type, DT_DIR);
	n = ops->readdir("/sys/devices", ents, 16);
	ASSERT_EQ(n, 2);
	ASSERT_STR_EQ(ents[0].name, "system");
	ASSERT_EQ(ents[0].type, DT_REG);
	ASSERT_STR_EQ(ents[1].name, "block");
	/* Exact file nodes stay files, so getdents is ENOTDIR even with children. */
	ASSERT_EQ(ops->readdir("/sys/devices/system", ents, 16), -ENOTDIR);
	ASSERT_EQ(ops->stat("/sys/devices/system/cpu", &st), 0);
	ASSERT(S_ISREG(st.st_mode));
	ASSERT_EQ(ops->readdir("/sys/devices/system/cpu", ents, 16), -ENOTDIR);
	ASSERT_EQ(ops->readdir("/sys/kernel/version", ents, 16), -ENOTDIR);
	ASSERT_EQ(ops->readdir("/sys", ents, 2), 2);

	/* Read slices the generated text; write ignores the offset. */
	memset(buf, 0, sizeof(buf));
	ASSERT_EQ(read_at("/sys/kernel/version", buf, 4, 0, &done), 0);
	ASSERT_EQ(done, (size_t)4);
	ASSERT_STR_EQ(buf, "ir0-");
	ASSERT_EQ(read_at("/sys/kernel/version", buf, 8, 4, &done), 0);
	ASSERT_EQ(done, (size_t)5);
	ASSERT_STR_EQ(buf, "test\n");
	ASSERT_EQ(read_at("/sys/kernel/version", buf, 4, 9, &done), 0);
	ASSERT_EQ(done, (size_t)0);
	ASSERT_EQ(read_at("/sys/kernel/version", buf, 4, -1, &done), -EINVAL);
	ASSERT_EQ(read_at("/sys/kernel", buf, 4, 0, &done), -EISDIR);
	ASSERT_EQ(read_at("/sys/missing", buf, 4, 0, &done), -ENOENT);
	memset(buf, 0, sizeof(buf));
	ASSERT_EQ(read_at("/sys/devices/system/cpu/cpu1/online", buf, 8, 0,
			  &done), 0);
	ASSERT_STR_EQ(buf, "1\n");

	ASSERT_EQ(ops->write("/sys/kernel/version", "x", 1, &done, 0), -EINVAL);
	ASSERT_EQ(ops->write("/sys/kernel/hostname", "box\n", 4, &done, 99), 0);
	ASSERT_EQ(done, (size_t)4);
	memset(buf, 0, sizeof(buf));
	ASSERT_EQ(read_at("/sys/kernel/hostname", buf, sizeof(buf), 0, &done), 0);
	ASSERT_STR_EQ(buf, "box\n");
	ASSERT_EQ(ops->write("/sys/devices/system/cpu/cpu0/online", "0\n", 2,
			     &done, 0), -EOPNOTSUPP);
	ASSERT_EQ(ops->write("/sys/kernel", "x", 1, &done, 0), -EISDIR);

	/* Open: directories need O_DIRECTORY; a file with children does not open. */
	ASSERT_EQ(ops->open("/sys", 0), -EISDIR);
	ASSERT_EQ(ops->open("/sys", IR0_O_DIRECTORY), 0);
	ASSERT_EQ(ops->open("/sys/kernel/version", 0), 0);
	ASSERT_EQ(ops->open("/sys/kernel/version", IR0_O_DIRECTORY), -ENOTDIR);
	ASSERT_EQ(ops->open("/sys/devices/system", 0), -EISDIR);
	ASSERT_EQ(ops->open("/sys/devices/system", IR0_O_DIRECTORY), -ENOTDIR);
	ASSERT_EQ(ops->open("/sys/devices/system/cpu/cpu0/online", 0), 0);
	ASSERT_EQ(ops->open("/sys/nope", 0), -ENOENT);
	ASSERT_EQ(ops->truncate("/sys/kernel/hostname", 0), 0);
	ASSERT_EQ(ops->truncate("/sys/kernel", 0), -EISDIR);
	ASSERT_EQ(ops->create("/sys/new", 0644), -ENOENT);

	/* /heart/sys sees exact nodes only, not synthesized directories. */
	ASSERT_EQ(sysfs_alias_stat("/sys/kernel", &st), -ENOENT);
	ASSERT_EQ(sysfs_alias_stat("/sys/kernel/hostname", &st), 0);
	ASSERT(S_ISREG(st.st_mode));
	ASSERT_EQ(sysfs_alias_open("/sys/kernel", 0), -ENOENT);
	ASSERT_EQ(sysfs_alias_open("/sys/kernel/version", 0), 0);

	TEST_END();
}
