/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mount_probe.c
 * Description: Minimal tmpfs mount/umount workload for Linux↔IR0 ABI audit
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#define MNT_PATH "/tmp/ir0mnt"
#define MNT_NOENT "/tmp/ir0mnt_nope"
#define BAD_FSTYPE "badfs"
#define RW_MSG "mntok\n"
#define RW_LEN 6U

static void audit_mount(unsigned step, const char *op, long ret, int err,
			const char *path, const char *data_hex)
{
	char buf[320];
	int n;

	if (data_hex)
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][mount] step=%u op=%s ret=%ld errno=%d path=%s data_hex=%s\n",
			     step, op, ret, err, path ? path : "-", data_hex);
	}
	else
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][mount] step=%u op=%s ret=%ld errno=%d path=%s\n",
			     step, op, ret, err, path ? path : "-");
	}
	if (n > 0)
		(void)write(1, buf, (size_t)n);
}

static int hex_encode(const unsigned char *data, unsigned len, char *hex,
		      size_t hex_sz)
{
	unsigned i;

	if (hex_sz < (len * 2U + 1U))
		return -1;
	for (i = 0U; i < len; i++)
		sprintf(hex + (i * 2U), "%02x", data[i]);
	hex[len * 2U] = '\0';
	return 0;
}

int main(void)
{
	long ret;
	char hex[RW_LEN * 2U + 1U];
	char buf[32];
	int fd;
	ssize_t n;

	(void)mkdir(MNT_PATH, 0755);

	errno = 0;
	ret = (long)mount("none", MNT_PATH, "tmpfs", 0, NULL);
	audit_mount(0, "mount_tmpfs", ret, ret < 0 ? errno : 0, MNT_PATH, NULL);
	if (ret < 0)
		return 1;

	fd = open(MNT_PATH "/probe.txt", O_CREAT | O_TRUNC | O_RDWR, 0644);
	if (fd < 0)
		return 1;
	if (write(fd, RW_MSG, RW_LEN) != (ssize_t)RW_LEN)
	{
		close(fd);
		return 1;
	}
	if (lseek(fd, 0, SEEK_SET) < 0)
	{
		close(fd);
		return 1;
	}
	memset(buf, 0, sizeof(buf));
	n = read(fd, buf, sizeof(buf) - 1U);
	close(fd);
	if (n != (ssize_t)RW_LEN || memcmp(buf, RW_MSG, RW_LEN) != 0)
		return 1;
	if (hex_encode((unsigned char *)buf, RW_LEN, hex, sizeof(hex)) < 0)
		return 1;
	audit_mount(1, "tmpfs_rw", 0, 0, MNT_PATH "/probe.txt", hex);

	errno = 0;
	ret = (long)umount(MNT_PATH);
	audit_mount(2, "umount_tmpfs", ret, ret < 0 ? errno : 0, MNT_PATH, NULL);
	if (ret < 0)
		return 1;

	errno = 0;
	ret = (long)mount("none", MNT_NOENT, "tmpfs", 0, NULL);
	audit_mount(3, "mount_noent", ret, errno, MNT_NOENT, NULL);
	if (ret >= 0 || errno != ENOENT)
		return 1;

	errno = 0;
	ret = (long)mount("none", MNT_PATH, BAD_FSTYPE, 0, NULL);
	audit_mount(4, "mount_badfs", ret, errno, MNT_PATH, NULL);
	if (ret >= 0 || errno != ENODEV)
		return 1;

	(void)write(1, "[MOUNTOK]\n", 10);
	return 0;
}
