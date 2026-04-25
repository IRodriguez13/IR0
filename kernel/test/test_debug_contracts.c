/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_debug_contracts.c
 * Description: Contract tests for proc/sys/dev endpoints consumed by debug_bins.
 */

#include "test/ktest_harness.h"
#include "syscalls.h"
#include "debug_bins/debug_bins.h"
#include <config.h>
#include <ir0/errno.h>
#include <ir0/fcntl.h>
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
	KASSERT_GT(fd, 0);
	if (fd > 0)
	{
		char buf[256];
		memset(buf, 0, sizeof(buf));
		int64_t n = sys_read((int)fd, buf, sizeof(buf) - 1);
		sys_close((int)fd);
		KASSERT_GE(n, 0);
	}
#else
	KASSERT(fd < 0);
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
		char buf[256];
		memset(buf, 0, sizeof(buf));
		int64_t n = sys_read((int)fd, buf, sizeof(buf) - 1);
		sys_close((int)fd);
		KASSERT_GE(n, 0);
	}
#else
	KASSERT(fd < 0);
#endif

	KTEST_END();
}

void ktest_help_sections_contract(void)
{
	KTEST_BEGIN("help_sections_contract");

	KASSERT(debug_is_valid_section("shell"));
	KASSERT(debug_is_valid_section("core"));
	KASSERT(debug_is_valid_section("diag"));
	KASSERT(!debug_is_valid_section("unknown_section"));

	KASSERT(strcmp(debug_command_section("ls"), "core") == 0);
	KASSERT(strcmp(debug_command_section("mkdir"), "fs") == 0);
	KASSERT(debug_command_section("help") == NULL);

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

	int64_t mk = sys_mkdir("/mntkt", 0755);
	KASSERT(mk == 0 || mk == -EEXIST);

	int64_t ret = sys_mount("none", "/mntkt", "tmpfs");
	KASSERT_EQ(ret, 0);

	int64_t dup = sys_mount("none", "/mntkt", "tmpfs");
	KASSERT(dup < 0);

	int64_t bad = sys_mount("none", "/mntkt", "badfs");
	KASSERT(bad < 0);

	int64_t file_fd = sys_open("/mntkt/mount_probe.txt", O_CREAT | O_RDWR | O_TRUNC, 0644);
	KASSERT_GT(file_fd, 0);
	const char *msg = "tmpfs-mount-ok";
	int64_t w = sys_write((int)file_fd, msg, strlen(msg));
	KASSERT_GT(w, 0);
	sys_close((int)file_fd);

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

	int64_t mk_mnt = sys_mkdir("/mnt", 0755);
	KASSERT(mk_mnt == 0 || mk_mnt == -EEXIST);
	int64_t mk_simple = sys_mkdir("/mnt/simple", 0755);
	KASSERT(mk_simple == 0 || mk_simple == -EEXIST);
	int64_t mk_fat = sys_mkdir("/mnt/fat", 0755);
	KASSERT(mk_fat == 0 || mk_fat == -EEXIST);

	int64_t ms = sys_mount("/dev/simple0", "/mnt/simple", "simplefs");
	KASSERT(ms == 0 || ms == -EBUSY);
	int64_t mf = sys_mount("/dev/fat0", "/mnt/fat", "fat16");
	KASSERT(mf == 0 || mf == -EBUSY);

	int64_t sfd = sys_open("/mnt/simple/s1.txt", O_CREAT | O_RDWR | O_TRUNC, 0644);
	KASSERT_GT(sfd, 0);
	const char *smsg = "simplefs-ok";
	int64_t sw = sys_write((int)sfd, smsg, strlen(smsg));
	KASSERT_GT(sw, 0);
	sys_close((int)sfd);

	int64_t ffd = sys_open("/mnt/fat/HELLO.TXT", O_CREAT | O_RDWR | O_TRUNC, 0644);
	KASSERT_GT(ffd, 0);
	const char *fmsg = "fat16-ok";
	int64_t fw = sys_write((int)ffd, fmsg, strlen(fmsg));
	KASSERT_GT(fw, 0);
	sys_close((int)ffd);

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

void ktest_mount_longest_prefix_contract(void)
{
	KTEST_BEGIN("mount_longest_prefix_contract");

	int64_t mk_lp = sys_mkdir("/mnt/lp", 0755);
	KASSERT(mk_lp == 0 || mk_lp == -EEXIST);

	int64_t mt = sys_mount("none", "/mnt/lp", "tmpfs");
	KASSERT(mt == 0 || mt == -EBUSY);

	int64_t mk_sub = sys_mkdir("/mnt/lp/sub", 0755);
	KASSERT(mk_sub == 0 || mk_sub == -EEXIST);

	int64_t ms = sys_mount("/dev/simplelp", "/mnt/lp/sub", "simplefs");
	KASSERT(ms == 0 || ms == -EBUSY);

	int64_t ofd = sys_open("/mnt/lp/outer.txt", O_CREAT | O_RDWR | O_TRUNC, 0644);
	KASSERT_GT(ofd, 0);
	const char *omsg = "outer-tmpfs";
	KASSERT_GT(sys_write((int)ofd, omsg, strlen(omsg)), 0);
	sys_close((int)ofd);

	int64_t ifd = sys_open("/mnt/lp/sub/INNER.TXT", O_CREAT | O_RDWR | O_TRUNC, 0644);
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

	KTEST_END();
}
