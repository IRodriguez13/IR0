/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: init_ext2_smoke.c
 * Description: Userspace smoke — mount read-only EXT2 on hdb and read HELLO.TXT
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#define MOUNT_POINT "/ext2mnt"
#define EXT_DEVICE  "/dev/hdb"
#define TEST_FILE   "/ext2mnt/HELLO.TXT"
#define EXPECT      "EXT2-SMOKE-OK\n"

static void write_str(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

int main(void)
{
	char buf[64];
	ssize_t n;
	int fd;

	if (mkdir(MOUNT_POINT, 0755) != 0 && errno != EEXIST)
		return 2;
	if (mount(EXT_DEVICE, MOUNT_POINT, "ext2", 0, NULL) != 0)
	{
		write_str("[EXT2][FAIL] mount\n");
		return 3;
	}
	fd = open(TEST_FILE, O_RDONLY);
	if (fd < 0)
	{
		write_str("[EXT2][FAIL] open\n");
		return 4;
	}
	memset(buf, 0, sizeof(buf));
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n < 0 || (size_t)n != strlen(EXPECT) || memcmp(buf, EXPECT, (size_t)n) != 0)
	{
		write_str("[EXT2][FAIL] verify\n");
		return 5;
	}
	write_str("[EXT2] CLASSIFY EXT2_MOUNT_READ_OK\n");
	write_str("[EXT2OK]\n");
	return 0;
}
