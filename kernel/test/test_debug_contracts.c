/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_debug_contracts.c
 * Description: Contract tests for proc/sys/dev endpoints.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test/ktest_harness.h"
#include "syscalls.h"
#include "../../fs/tmpfs.h"
#include <config.h>
#include <ir0/blockdev.h>
#include <ir0/errno.h>
#include <ir0/fcntl.h>
#include <ir0/net.h>
#include <ir0/stat.h>
#include <ir0/statfs.h>
#include <ir0/vfs.h>
#include <string.h>

static int count_tabs(const char *line)
{
	int tabs = 0;

	if (!line)
		return 0;
	while (*line && *line != '\n')
	{
		if (*line == '\t')
			tabs++;
		line++;
	}
	return tabs;
}

static int contains_text(const char *haystack, const char *needle)
{
	if (!haystack || !needle || !needle[0])
		return 0;
	const char *p = haystack;
	size_t nlen = strlen(needle);
	while (*p)
	{
		size_t i = 0;
		while (p[i] && i < nlen && p[i] == needle[i])
			i++;
		if (i == nlen)
			return 1;
		p++;
	}
	return 0;
}

static void ktest_ensure_mountpoint_dir(const char *path)
{
	stat_t st;
	int mk;

	if (vfs_stat(path, &st) == 0 && !S_ISDIR(st.st_mode))
		(void)vfs_unlink(path);

	mk = vfs_mkdir(path, 0755);
	KASSERT(mk == 0 || mk == -EEXIST);
	if (mk == -EEXIST)
	{
		KASSERT(vfs_stat(path, &st) == 0);
		KASSERT(S_ISDIR(st.st_mode));
	}
}

void ktest_proc_blockdevices_contract(void)
{
	KTEST_BEGIN("proc_blockdevices_contract");

	int64_t fd = sys_open("/proc/blockdevices", O_RDONLY, 0);
	KASSERT_GT(fd, 0);

	char buf[512];
	memset(buf, 0, sizeof(buf));
	int64_t n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	KASSERT_GT(n, 0);

	const char *line = buf;
	while (*line == '\n')
		line++;
	if (*line != '\0')
	{
		/* type, name, maj, min, sectors, size_human, model, serial => 7 tabs */
		KASSERT_GE(count_tabs(line), 7);
	}

	KTEST_END();
}

void ktest_proc_cpuinfo_contract(void)
{
	KTEST_BEGIN("proc_cpuinfo_contract");

	int64_t fd = sys_open("/proc/cpuinfo", O_RDONLY, 0);
	KASSERT_GT(fd, 0);

	char buf[1024];
	memset(buf, 0, sizeof(buf));
	int64_t n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	KASSERT_GT(n, 0);
	KASSERT(contains_text(buf, "vendor_id\t"));
	KASSERT(contains_text(buf, "model name\t"));

	KTEST_END();
}

void ktest_proc_version_contract(void)
{
	KTEST_BEGIN("proc_version_contract");

	int64_t fd = sys_open("/proc/version", O_RDONLY, 0);
	KASSERT_GT(fd, 0);

	char buf[256];
	memset(buf, 0, sizeof(buf));
	int64_t n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	KASSERT_GT(n, 0);
	KASSERT(buf[0] != '\0');
	/* Human-readable banner (not a fixed tab-separated schema). */
	KASSERT(contains_text(buf, "IR0") || contains_text(buf, "Linux"));
	KASSERT(contains_text(buf, "#"));

	KTEST_END();
}

void ktest_proc_uptime_contract(void)
{
	KTEST_BEGIN("proc_uptime_contract");

	int64_t fd = sys_open("/proc/uptime", O_RDONLY, 0);
	KASSERT_GT(fd, 0);

	char buf[64];
	memset(buf, 0, sizeof(buf));
	int64_t n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	KASSERT_GT(n, 0);
	KASSERT(buf[0] >= '0' && buf[0] <= '9');

	KTEST_END();
}

void ktest_sysfs_hostname_contract(void)
{
	KTEST_BEGIN("sysfs_hostname_contract");

	int64_t fd = sys_open("/sys/kernel/hostname", O_RDONLY, 0);
	KASSERT_GT(fd, 0);

	char buf[128];
	memset(buf, 0, sizeof(buf));
	int64_t n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	KASSERT_GT(n, 0);
	KASSERT(buf[0] != '\0');

	KTEST_END();
}

void ktest_proc_netinfo_contract(void)
{
	KTEST_BEGIN("proc_netinfo_contract");

	int64_t fd = sys_open("/proc/netinfo", O_RDONLY, 0);
#if CONFIG_ENABLE_NETWORKING
	struct net_device fake;
	char buf[256];
	int64_t n;
	const char *line;
	int found = 0;
	static const char expected[] =
		"ktest0\t1500\tUP,RUNNING,BROADCAST\t02:00:00:00:00:01";

	/*
	 * /proc/netinfo is registered even when no NIC is present, so an
	 * empty read proves nothing.  Register a fake device (unregistered
	 * before returning) and require its exact tab-separated row.
	 */
	memset(&fake, 0, sizeof(fake));
	fake.name = "ktest0";
	fake.mtu = 1500;
	fake.flags = IFF_UP | IFF_RUNNING | IFF_BROADCAST;
	fake.mac[0] = 0x02;
	fake.mac[5] = 0x01;

	KASSERT_EQ(net_register_device(&fake), 0);
	KASSERT_GT(fd, 0);
	if (fd > 0)
	{
		memset(buf, 0, sizeof(buf));
		n = sys_read((int)fd, buf, sizeof(buf) - 1);
		sys_close((int)fd);
		KASSERT_GT(n, 0);
		KASSERT(n < (int64_t)sizeof(buf));

		line = buf;
		while (*line)
		{
			const char *nl = strchr(line, '\n');

			if (!nl)
				break;
			if ((size_t)(nl - line) == sizeof(expected) - 1 &&
			    strncmp(line, expected, sizeof(expected) - 1) == 0)
			{
				found = 1;
				break;
			}
			line = nl + 1;
		}
		KASSERT(found);
	}
	net_unregister_device(&fake);

	/* The row must disappear once the device is gone. */
	fd = sys_open("/proc/netinfo", O_RDONLY, 0);
	KASSERT_GT(fd, 0);
	if (fd > 0)
	{
		memset(buf, 0, sizeof(buf));
		n = sys_read((int)fd, buf, sizeof(buf) - 1);
		sys_close((int)fd);
		KASSERT_GE(n, 0);
		KASSERT(!contains_text(buf, "ktest0"));
	}
#else
	/*
	 * The node is registered unconditionally; without networking the
	 * reader returns an empty file rather than making open() fail.
	 */
	KASSERT_GT(fd, 0);
	if (fd > 0)
	{
		char buf[64];
		int64_t n;

		memset(buf, 0, sizeof(buf));
		n = sys_read((int)fd, buf, sizeof(buf) - 1);
		sys_close((int)fd);
		KASSERT_EQ(n, 0);
	}
#endif

	KTEST_END();
}

void ktest_dev_net_contract(void)
{
	KTEST_BEGIN("dev_net_contract");

	int64_t fd = sys_open("/dev/net", O_RDONLY, 0);
#if CONFIG_ENABLE_NETWORKING
	KASSERT_GT(fd, 0);
	if (fd > 0)
	{
		char buf[2048];
		char one;
		size_t total = 0;
		int64_t n;
		int64_t n2;
		int64_t fd2;
		char buf2[64];

		memset(buf, 0, sizeof(buf));
		/* Full read then persistent EOF. */
		n = sys_read((int)fd, buf, sizeof(buf) - 1);
		KASSERT_GT(n, 0);
		KASSERT(contains_text(buf, "type="));
		n2 = sys_read((int)fd, buf, sizeof(buf) - 1);
		KASSERT_EQ(n2, 0);
		sys_close((int)fd);

		/* Byte-at-a-time read reaches EOF. */
		fd = sys_open("/dev/net", O_RDONLY, 0);
		KASSERT_GT(fd, 0);
		total = 0;
		for (;;)
		{
			n = sys_read((int)fd, &one, 1);
			if (n == 0)
				break;
			KASSERT_EQ(n, 1);
			total++;
			KASSERT(total < sizeof(buf));
		}
		KASSERT_GT((int64_t)total, 0);
		n2 = sys_read((int)fd, &one, 1);
		KASSERT_EQ(n2, 0);

		/* Independent opens: second open has its own offset. */
		fd2 = sys_open("/dev/net", O_RDONLY, 0);
		KASSERT_GT(fd2, 0);
		memset(buf2, 0, sizeof(buf2));
		n = sys_read((int)fd2, buf2, sizeof(buf2) - 1);
		KASSERT_GT(n, 0);
		sys_close((int)fd2);
		sys_close((int)fd);
	}
#else
	KASSERT(fd < 0);
#endif

	KTEST_END();
}


void ktest_mount_proc_contract(void)
{
	KTEST_BEGIN("mount_proc_contract");

	int64_t fd = sys_open("/proc/mounts", O_RDONLY, 0);
	KASSERT_GT(fd, 0);

	char buf[512];
	memset(buf, 0, sizeof(buf));
	int64_t n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	KASSERT_GT(n, 0);
	KASSERT(contains_text(buf, " / "));

	KTEST_END();
}

void ktest_mount_tmpfs_contract(void)
{
	KTEST_BEGIN("mount_tmpfs_contract");

	ktest_ensure_mountpoint_dir("/mntkt");

	int64_t ret = sys_mount("none", "/mntkt", "tmpfs", 0, NULL);
	if (ret == -EBUSY)
	{
		(void)sys_umount("/mntkt", 0);
		ret = sys_mount("none", "/mntkt", "tmpfs", 0, NULL);
	}
	KASSERT_EQ(ret, 0);

	struct ir0_statfs empty_fs;
	memset(&empty_fs, 0, sizeof(empty_fs));
	KASSERT_EQ(vfs_statfs("/mntkt", &empty_fs), 0);
	KASSERT_EQ(empty_fs.f_type, IR0_TMPFS_MAGIC);
	KASSERT(empty_fs.f_blocks > 0);

	char long_tmpfs_name[sizeof("/mntkt/") + 256];
	memcpy(long_tmpfs_name, "/mntkt/", sizeof("/mntkt/") - 1);
	memset(long_tmpfs_name + sizeof("/mntkt/") - 1, 'x', 256);
	long_tmpfs_name[sizeof(long_tmpfs_name) - 1] = '\0';
	KASSERT_EQ(tmpfs_create_file(long_tmpfs_name, 0644), -ENAMETOOLONG);

	int64_t dup = sys_mount("none", "/mntkt", "tmpfs", 0, NULL);
	KASSERT(dup < 0);

	int64_t bad = sys_mount("none", "/mntkt", "badfs", 0, NULL);
	KASSERT(bad < 0);

	int64_t file_fd = sys_open("/mntkt/mount_probe.txt",
				   KTEST_O_CREAT | KTEST_O_RDWR | KTEST_O_TRUNC, 0644);
	KASSERT_GT(file_fd, 0);
	const char *msg = "tmpfs-mount-ok";
	int64_t w = sys_write((int)file_fd, msg, strlen(msg));
	KASSERT_EQ(w, (int64_t)strlen(msg));
	sys_close((int)file_fd);

	struct ir0_statfs written_fs;
	memset(&written_fs, 0, sizeof(written_fs));
	KASSERT_EQ(vfs_statfs("/mntkt/mount_probe.txt", &written_fs), 0);
	KASSERT_EQ(written_fs.f_bfree + 1, empty_fs.f_bfree);

	/* The file must survive the close and read back from the mounted fs. */
	file_fd = sys_open("/mntkt/mount_probe.txt", O_RDONLY, 0);
	KASSERT_GT(file_fd, 0);
	char readback[64];
	memset(readback, 0, sizeof(readback));
	int64_t r = sys_read((int)file_fd, readback, sizeof(readback) - 1);
	sys_close((int)file_fd);
	KASSERT_EQ(r, (int64_t)strlen(msg));
	KASSERT(strcmp(readback, msg) == 0);

	/* Hardlinks share one inode and survive removal of either directory entry. */
	KASSERT_EQ(sys_link("/mntkt/mount_probe.txt", "/mntkt/mount_alias.txt"), 0);
	stat_t original_st;
	stat_t alias_st;
	KASSERT_EQ(vfs_stat("/mntkt/mount_probe.txt", &original_st), 0);
	KASSERT_EQ(vfs_stat("/mntkt/mount_alias.txt", &alias_st), 0);
	KASSERT_EQ(original_st.st_ino, alias_st.st_ino);
	KASSERT_EQ(original_st.st_nlink, (nlink_t)2);
	KASSERT_EQ(alias_st.st_nlink, (nlink_t)2);
	struct ir0_statfs linked_fs;
	memset(&linked_fs, 0, sizeof(linked_fs));
	KASSERT_EQ(vfs_statfs("/mntkt/mount_alias.txt", &linked_fs), 0);
	KASSERT_EQ(linked_fs.f_ffree, written_fs.f_ffree);
	KASSERT_EQ(linked_fs.f_bfree, written_fs.f_bfree);
	int64_t alias_fd = sys_open("/mntkt/mount_alias.txt", KTEST_O_RDWR, 0);
	KASSERT_GT(alias_fd, 0);
	KASSERT_EQ(sys_write((int)alias_fd, "alias", 5), 5);
	sys_close((int)alias_fd);
	file_fd = sys_open("/mntkt/mount_probe.txt", O_RDONLY, 0);
	KASSERT_GT(file_fd, 0);
	memset(readback, 0, sizeof(readback));
	KASSERT_EQ(sys_read((int)file_fd, readback, 5), 5);
	sys_close((int)file_fd);
	KASSERT(memcmp(readback, "alias", 5) == 0);
	KASSERT_EQ(tmpfs_chmod("/mntkt/mount_alias.txt", 0600), 0);
	KASSERT_EQ(vfs_stat("/mntkt/mount_probe.txt", &original_st), 0);
	KASSERT_EQ(original_st.st_mode & (mode_t)0777, (mode_t)0600);
	KASSERT_EQ(sys_truncate("/mntkt/mount_alias.txt", 5), 0);
	KASSERT_EQ(vfs_stat("/mntkt/mount_probe.txt", &original_st), 0);
	KASSERT_EQ(original_st.st_size, 5);
	KASSERT_EQ(sys_unlink("/mntkt/mount_probe.txt"), 0);
	KASSERT(vfs_stat("/mntkt/mount_probe.txt", &original_st) < 0);
	KASSERT_EQ(vfs_stat("/mntkt/mount_alias.txt", &alias_st), 0);
	KASSERT_EQ(alias_st.st_nlink, (nlink_t)1);

	KASSERT_EQ(sys_truncate("/mntkt/mount_alias.txt", 0), 0);
	struct ir0_statfs truncated_fs;
	memset(&truncated_fs, 0, sizeof(truncated_fs));
	KASSERT_EQ(vfs_statfs("/mntkt/mount_alias.txt", &truncated_fs), 0);
	KASSERT_EQ(truncated_fs.f_bfree, empty_fs.f_bfree);

	char mounts[1024];
	memset(mounts, 0, sizeof(mounts));
	int64_t mfd = sys_open("/proc/mounts", O_RDONLY, 0);
	KASSERT_GT(mfd, 0);
	int64_t rn = sys_read((int)mfd, mounts, sizeof(mounts) - 1);
	sys_close((int)mfd);
	KASSERT_GT(rn, 0);
	KASSERT(contains_text(mounts, "/mntkt tmpfs"));

	KTEST_END();
}

void ktest_mount_multi_fs_contract(void)
{
	KTEST_BEGIN("mount_multi_fs_contract");

	ktest_ensure_mountpoint_dir("/mnt");
	ktest_ensure_mountpoint_dir("/mnt/simple");
	ktest_ensure_mountpoint_dir("/mnt/fat");

	int64_t ms = sys_mount("/dev/simple0", "/mnt/simple", "simplefs", 0, NULL);
	KASSERT(ms == 0 || ms == -EBUSY);
	int64_t mf = sys_mount("/dev/fat0", "/mnt/fat", "fat16", 0, NULL);
	KASSERT(mf == 0 || mf == -EBUSY);

	int64_t sfd = sys_open("/mnt/simple/s1.txt",
			       KTEST_O_CREAT | KTEST_O_RDWR | KTEST_O_TRUNC, 0644);
	KASSERT_GT(sfd, 0);
	const char *smsg = "simplefs-ok";
	int64_t sw = sys_write((int)sfd, smsg, strlen(smsg));
	KASSERT_EQ(sw, (int64_t)strlen(smsg));
	sys_close((int)sfd);

	int64_t ffd = sys_open("/mnt/fat/HELLO.TXT",
			       KTEST_O_CREAT | KTEST_O_RDWR | KTEST_O_TRUNC, 0644);
	KASSERT_GT(ffd, 0);
	const char *fmsg = "fat16-ok";
	int64_t fw = sys_write((int)ffd, fmsg, strlen(fmsg));
	KASSERT_EQ(fw, (int64_t)strlen(fmsg));
	sys_close((int)ffd);

	char sread[64];
	char fread_buf[64];
	memset(sread, 0, sizeof(sread));
	memset(fread_buf, 0, sizeof(fread_buf));
	sfd = sys_open("/mnt/simple/s1.txt", O_RDONLY, 0);
	KASSERT_GT(sfd, 0);
	KASSERT_EQ(sys_read((int)sfd, sread, sizeof(sread) - 1),
		   (int64_t)strlen(smsg));
	sys_close((int)sfd);
	ffd = sys_open("/mnt/fat/HELLO.TXT", O_RDONLY, 0);
	KASSERT_GT(ffd, 0);
	KASSERT_EQ(sys_read((int)ffd, fread_buf, sizeof(fread_buf) - 1),
		   (int64_t)strlen(fmsg));
	sys_close((int)ffd);
	KASSERT(strcmp(sread, smsg) == 0);
	KASSERT(strcmp(fread_buf, fmsg) == 0);

	char mounts[1024];
	memset(mounts, 0, sizeof(mounts));
	int64_t mfd = sys_open("/proc/mounts", O_RDONLY, 0);
	KASSERT_GT(mfd, 0);
	int64_t rn = sys_read((int)mfd, mounts, sizeof(mounts) - 1);
	sys_close((int)mfd);
	KASSERT_GT(rn, 0);
	KASSERT(contains_text(mounts, "/mnt/simple simplefs"));
	KASSERT(contains_text(mounts, "/mnt/fat fat16"));

	KTEST_END();
}

void ktest_mount_umount_remount_contract(void)
{
	KTEST_BEGIN("mount_umount_remount_contract");

	ktest_ensure_mountpoint_dir("/mnt/um");
	ktest_ensure_mountpoint_dir("/mnt/umfat");

	int64_t m1 = sys_mount("/dev/simple0", "/mnt/um", "simplefs", 0, NULL);
	if (m1 == -EBUSY)
	{
		(void)sys_umount("/mnt/um", 0);
		m1 = sys_mount("/dev/simple0", "/mnt/um", "simplefs", 0, NULL);
	}
	KASSERT_EQ(m1, 0);

	int64_t u = sys_umount("/mnt/um", 0);
	KASSERT_EQ(u, 0);

	int64_t m2 = sys_mount("/dev/simple0", "/mnt/um", "simplefs", 0, NULL);
	KASSERT_EQ(m2, 0);

	int64_t mf1 = sys_mount("/dev/fat0", "/mnt/umfat", "fat16", 0, NULL);
	if (mf1 == -EBUSY)
	{
		(void)sys_umount("/mnt/umfat", 0);
		mf1 = sys_mount("/dev/fat0", "/mnt/umfat", "fat16", 0, NULL);
	}
	KASSERT_EQ(mf1, 0);

	int64_t uf = sys_umount("/mnt/umfat", 0);
	KASSERT_EQ(uf, 0);

	int64_t mf2 = sys_mount("/dev/fat0", "/mnt/umfat", "fat16", 0, NULL);
	KASSERT_EQ(mf2, 0);

	/* Linux mount(2) remount via MS_REMOUNT|MS_RDONLY (flags arg). */
	int64_t rro = sys_mount(NULL, "/mnt/um", NULL,
				IR0_MS_REMOUNT | IR0_MS_RDONLY, NULL);
	KASSERT_EQ(rro, 0);
	int64_t rrw = sys_mount("ignored", "/mnt/um", "ignored",
				IR0_MS_REMOUNT, NULL);
	KASSERT_EQ(rrw, 0);

	KTEST_END();
}

void ktest_mount_longest_prefix_contract(void)
{
	KTEST_BEGIN("mount_longest_prefix_contract");

	ktest_ensure_mountpoint_dir("/mnt/lp");

	int64_t mt = sys_mount("none", "/mnt/lp", "tmpfs", 0, NULL);
	KASSERT(mt == 0 || mt == -EBUSY);

	ktest_ensure_mountpoint_dir("/mnt/lp/sub");

	int64_t ms = sys_mount("/dev/simplelp", "/mnt/lp/sub", "simplefs", 0, NULL);
	KASSERT(ms == 0 || ms == -EBUSY);

	int64_t ofd = sys_open("/mnt/lp/outer.txt",
			       KTEST_O_CREAT | KTEST_O_RDWR | KTEST_O_TRUNC, 0644);
	KASSERT_GT(ofd, 0);
	const char *omsg = "outer-tmpfs";
	KASSERT_GT(sys_write((int)ofd, omsg, strlen(omsg)), 0);
	sys_close((int)ofd);

	int64_t ifd = sys_open("/mnt/lp/sub/INNER.TXT",
			       KTEST_O_CREAT | KTEST_O_RDWR | KTEST_O_TRUNC, 0644);
	KASSERT_GT(ifd, 0);
	const char *imsg = "inner-simplefs";
	KASSERT_GT(sys_write((int)ifd, imsg, strlen(imsg)), 0);
	sys_close((int)ifd);

	char outer_buf[64];
	char inner_buf[64];
	memset(outer_buf, 0, sizeof(outer_buf));
	memset(inner_buf, 0, sizeof(inner_buf));

	ofd = sys_open("/mnt/lp/outer.txt", O_RDONLY, 0);
	KASSERT_GT(ofd, 0);
	KASSERT_GT(sys_read((int)ofd, outer_buf, sizeof(outer_buf) - 1), 0);
	sys_close((int)ofd);

	ifd = sys_open("/mnt/lp/sub/INNER.TXT", O_RDONLY, 0);
	KASSERT_GT(ifd, 0);
	KASSERT_GT(sys_read((int)ifd, inner_buf, sizeof(inner_buf) - 1), 0);
	sys_close((int)ifd);

	KASSERT(contains_text(outer_buf, "outer-tmpfs"));
	KASSERT(contains_text(inner_buf, "inner-simplefs"));

	/* Same-backend nested mounts must also select the longest prefix. */
	ktest_ensure_mountpoint_dir("/mnt/lp/nested");
	int64_t mn = sys_mount("none", "/mnt/lp/nested", "tmpfs", 0, NULL);
	KASSERT(mn == 0 || mn == -EBUSY);
	int64_t nfd = sys_open("/mnt/lp/nested/private.txt",
			       KTEST_O_CREAT | KTEST_O_RDWR | KTEST_O_TRUNC, 0644);
	KASSERT_GT(nfd, 0);
	KASSERT_GT(sys_write((int)nfd, "nested", 6), 0);
	sys_close((int)nfd);
	KASSERT_EQ(sys_umount("/mnt/lp/nested", 0), 0);
	stat_t nested_st;
	KASSERT(vfs_stat("/mnt/lp/nested/private.txt", &nested_st) < 0);

	KTEST_END();
}

#define KTEST_BLK_SECTORS      4u
#define KTEST_BLK_SECTOR_SIZE  512u
#define KTEST_BLK_MAX_IO       2u
#define KTEST_BLK_NAME         "ktestblk"

static uint8_t ktest_blk_data[KTEST_BLK_SECTORS * KTEST_BLK_SECTOR_SIZE];
static unsigned ktest_blk_read_calls;

static int ktest_blk_read(void *ctx, uint64_t lba, uint32_t count, void *buf)
{
	size_t off;

	(void)ctx;
	if (!buf || count == 0)
		return -EINVAL;
	if (lba + (uint64_t)count > KTEST_BLK_SECTORS)
		return -EINVAL;
	ktest_blk_read_calls++;
	off = (size_t)lba * KTEST_BLK_SECTOR_SIZE;
	memcpy(buf, ktest_blk_data + off, (size_t)count * KTEST_BLK_SECTOR_SIZE);
	return 0;
}

static int ktest_blk_write(void *ctx, uint64_t lba, uint32_t count,
			   const void *buf)
{
	size_t off;

	(void)ctx;
	if (!buf || count == 0)
		return -EINVAL;
	if (lba + (uint64_t)count > KTEST_BLK_SECTORS)
		return -EINVAL;
	off = (size_t)lba * KTEST_BLK_SECTOR_SIZE;
	memcpy(ktest_blk_data + off, buf, (size_t)count * KTEST_BLK_SECTOR_SIZE);
	return 0;
}

static const struct ir0_block_ops ktest_blk_ops = {
	.read = ktest_blk_read,
	.write = ktest_blk_write,
	.flush = NULL,
};

/*
 * The block facade is the only path filesystems use. Exercise it with a
 * registered fake device: the real hda is absent under QEMU, so the hda
 * test alone never reads a sector.
 */
void ktest_block_layer_contract(void)
{
	static struct ir0_block_device dev;
	struct ir0_block_info info;
	dev_t id;
	uint8_t sectors[3 * KTEST_BLK_SECTOR_SIZE] __attribute__((aligned(4)));
	uint8_t pattern[KTEST_BLK_SECTOR_SIZE] __attribute__((aligned(4)));
	int found = -1;
	int i;

	KTEST_BEGIN("block_layer_contract");

	for (i = 0; i < (int)KTEST_BLK_SECTORS; i++)
		memset(ktest_blk_data + i * KTEST_BLK_SECTOR_SIZE, 0x40 + i,
		       KTEST_BLK_SECTOR_SIZE);
	ktest_blk_read_calls = 0;

	memset(&dev, 0, sizeof(dev));
	dev.ops = &ktest_blk_ops;
	dev.info.sector_size = KTEST_BLK_SECTOR_SIZE;
	dev.info.max_sectors_per_io = KTEST_BLK_MAX_IO;
	dev.info.sector_count = KTEST_BLK_SECTORS;
	dev.info.flags = 0;
	memcpy(dev.info.name, KTEST_BLK_NAME, sizeof(KTEST_BLK_NAME));

	KASSERT_EQ(ir0_block_register(&dev), 0);
	/* Registering the same name twice must not create a second slot. */
	KASSERT_EQ(ir0_block_register(&dev), -EEXIST);

	id = ir0_block_lookup_by_name(KTEST_BLK_NAME);
	KASSERT(id != 0);
	KASSERT(ir0_block_name_is_present(KTEST_BLK_NAME));
	KASSERT_EQ(ir0_block_sector_count_by_name(KTEST_BLK_NAME),
		   (uint64_t)KTEST_BLK_SECTORS);

	KASSERT_EQ(ir0_block_get_info(id, &info), 0);
	KASSERT_EQ(info.sector_size, KTEST_BLK_SECTOR_SIZE);
	KASSERT_EQ(info.max_sectors_per_io, KTEST_BLK_MAX_IO);
	KASSERT_EQ(info.sector_count, (uint64_t)KTEST_BLK_SECTORS);
	KASSERT(strcmp(info.name, KTEST_BLK_NAME) == 0);

	for (i = 0; i < ir0_block_count(); i++)
	{
		const char *name = ir0_block_name_at(i);

		if (name && strcmp(name, KTEST_BLK_NAME) == 0)
		{
			found = i;
			break;
		}
	}
	KASSERT_GE(found, 0);
	if (found >= 0)
		KASSERT_EQ(ir0_block_dev_at(found), id);

	/*
	 * max_sectors_per_io = 2 forces the facade to split this into two
	 * backend reads and to advance the LBA between them.
	 */
	memset(sectors, 0, sizeof(sectors));
	KASSERT_EQ(ir0_block_read(id, 0, 3, sectors), 0);
	KASSERT_EQ(ktest_blk_read_calls, 2u);
	KASSERT_EQ(memcmp(sectors, ktest_blk_data, sizeof(sectors)), 0);

	/* A write through the facade must be visible to the next read. */
	memset(pattern, 0xEE, sizeof(pattern));
	KASSERT_EQ(ir0_block_write_by_name(KTEST_BLK_NAME, 1, 1, pattern), 0);
	memset(sectors, 0, sizeof(sectors));
	KASSERT_EQ(ir0_block_read_by_name(KTEST_BLK_NAME, 1, 1, sectors), 0);
	KASSERT_EQ(memcmp(sectors, pattern, sizeof(pattern)), 0);

	/* Argument and range validation. */
	KASSERT_EQ(ir0_block_read(id, 0, 0, sectors), -EINVAL);
	KASSERT_EQ(ir0_block_read(id, 0, 1, NULL), -EINVAL);
	KASSERT_EQ(ir0_block_read(id, KTEST_BLK_SECTORS - 1, 2, sectors),
		   -EINVAL);
	KASSERT_EQ(ir0_block_read_by_name("ktest-missing", 0, 1, sectors),
		   -ENODEV);
	KASSERT_EQ(ir0_block_read_by_name(KTEST_BLK_NAME, 0, 1, NULL),
		   -EINVAL);
	KASSERT_EQ(ir0_block_get_info(0, &info), -EINVAL);

	/* flush is optional: a NULL backend hook is a successful no-op. */
	KASSERT_EQ(ir0_block_flush(id), 0);

	KTEST_END();
}

void ktest_block_hda_read_contract(void)
{
	char buf[512];
	int64_t fd;
	int64_t n;

	KTEST_BEGIN("block_hda_read_contract");

	/*
	 * hda presence depends on the host disk. When absent, assert the
	 * absence contract instead of ending silently; block_layer_contract
	 * covers the read path with a fake device either way.
	 */
	if (!ir0_block_name_is_present("hda"))
	{
		KASSERT_EQ(ir0_block_lookup_by_name("hda"), 0u);
		KASSERT_EQ(ir0_block_sector_count_by_name("hda"), 0ULL);
		KASSERT_EQ(ir0_block_read_by_name("hda", 0, 1, buf), -ENODEV);
		KTEST_END();
		return;
	}

	KASSERT_GT(ir0_block_sector_count_by_name("hda"), 0ULL);
	KASSERT_EQ(ir0_block_read_by_name("hda", 0, 1, buf), 0);

	fd = sys_open("/dev/hda", KTEST_O_RDONLY, 0);
	KASSERT_GT(fd, 0);
	if (fd <= 0)
	{
		KTEST_END();
		return;
	}

	memset(buf, 0, sizeof(buf));
	n = sys_read((int)fd, buf, sizeof(buf));
	sys_close((int)fd);
	KASSERT_EQ(n, (int64_t)sizeof(buf));

	{
		char one_a[512] __attribute__((aligned(4)));
		char one_b[512] __attribute__((aligned(4)));
		char two[1024] __attribute__((aligned(4)));

		memset(one_a, 0, sizeof(one_a));
		memset(one_b, 0, sizeof(one_b));
		memset(two, 0, sizeof(two));
		KASSERT_EQ(ir0_block_read_by_name("hda", 0, 1, one_a), 0);
		KASSERT_EQ(ir0_block_read_by_name("hda", 1, 1, one_b), 0);
		KASSERT_EQ(ir0_block_read_by_name("hda", 0, 2, two), 0);
		KASSERT_EQ(memcmp(two, one_a, 512), 0);
		KASSERT_EQ(memcmp(two + 512, one_b, 512), 0);
	}

	KTEST_END();
}

void ktest_devfs_hci_open_contract(void)
{
	KTEST_BEGIN("devfs_hci_open_contract");

#if CONFIG_ENABLE_BLUETOOTH
	{
		int64_t fd = sys_open("/dev/bluetooth/hci0", O_RDONLY, 0);
		KASSERT_GT(fd, 0);
		if (fd > 0)
			sys_close((int)fd);
	}
#else
	{
		int64_t fd = sys_open("/dev/bluetooth/hci0", O_RDONLY, 0);
		KASSERT(fd < 0);
	}
#endif

	KTEST_END();
}
