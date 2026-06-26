/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: init_fat16_smoke.c
 * Description: Userspace smoke — mount read-only FAT16 on hdb and read HELLO.TXT
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#define MOUNT_POINT "/fat16mnt"
#define FAT_DEVICE  "/dev/hdb"
#define TEST_FILE   "/fat16mnt/HELLO.TXT"
#define EXPECT      "FAT16-SMOKE-OK\n"

static void write_str(const char *s)
{
	const char *p = s;

	if (!s)
		return;
	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

static void fail_step(const char *step, int err)
{
	write_str("[FAT16][FAIL] step=");
	write_str(step);
	write_str(" errno=");
	if (err <= 0)
		write_str("0");
	else
	{
		char buf[12];
		int n = 0;
		unsigned u = (unsigned)err;

		do
		{
			buf[n++] = (char)('0' + (u % 10U));
			u /= 10U;
		} while (u > 0 && n < (int)sizeof(buf));
		while (n > 0)
			(void)write(1, &buf[--n], 1);
	}
	write_str("\n");
}

int main(void)
{
	char buf[64];
	ssize_t n;
	int fd;

	if (mkdir(MOUNT_POINT, 0755) != 0 && errno != EEXIST)
	{
		fail_step("mkdir", errno);
		return 2;
	}

	if (mount(FAT_DEVICE, MOUNT_POINT, "fat16", 0, NULL) != 0)
	{
		fail_step("mount", errno);
		return 3;
	}

	fd = open(TEST_FILE, O_RDONLY);
	if (fd < 0)
	{
		fail_step("open", errno);
		return 4;
	}

	memset(buf, 0, sizeof(buf));
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n < 0)
	{
		fail_step("read", errno);
		return 5;
	}
	if ((size_t)n != strlen(EXPECT) || memcmp(buf, EXPECT, (size_t)n) != 0)
	{
		fail_step("verify", 0);
		return 6;
	}

	write_str("[FAT16][CLASSIFY] FAT16_MOUNT_READ_OK\n");
	write_str("[FAT16OK]\n");
	return 0;
}
