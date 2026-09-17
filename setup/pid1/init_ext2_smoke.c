/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: init_ext2_smoke.c
 * Description: Userspace smoke — writable EXT2 semantics and persistence.
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
#define TEST_DIR    "/ext2mnt/home"
#define AUTH_TMP    "/ext2mnt/home/.serverauth.123-c"
#define AUTH_FINAL  "/ext2mnt/home/.serverauth.123"
#define AUTH_DATA   "0123456789abcdef\n"
#define LARGE_FILE  "/ext2mnt/home/indirect-blocks.bin"
#define LARGE_SIZE  (32 * 1024)

static void write_str(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

static int verify_large_file(void)
{
	unsigned char block[1024];
	int fd;
	int index;

	fd = open(LARGE_FILE, O_RDONLY);
	if (fd < 0)
		return -1;
	for (index = 0; index < LARGE_SIZE / (int)sizeof(block); index++)
	{
		int i;

		if (read(fd, block, sizeof(block)) != (ssize_t)sizeof(block))
		{
			close(fd);
			return -1;
		}
		for (i = 0; i < (int)sizeof(block); i++)
			if (block[i] != (unsigned char)(index ^ i))
			{
				close(fd);
				return -1;
			}
	}
	close(fd);
	return 0;
}

static int create_large_file(void)
{
	unsigned char block[1024];
	int fd;
	int index;

	fd = open(LARGE_FILE, O_CREAT | O_EXCL | O_WRONLY, 0600);
	if (fd < 0)
		return -1;
	for (index = 0; index < LARGE_SIZE / (int)sizeof(block); index++)
	{
		int i;

		for (i = 0; i < (int)sizeof(block); i++)
			block[i] = (unsigned char)(index ^ i);
		if (write(fd, block, sizeof(block)) != (ssize_t)sizeof(block))
		{
			close(fd);
			return -1;
		}
	}
	close(fd);
	return verify_large_file();
}

static int verify_xauth_stat_contract(void)
{
	struct stat st;

	if (stat(AUTH_FINAL, &st) != 0)
		return -1;
	if (st.st_ctime <= 0 || st.st_mtime <= 0 || st.st_blksize <= 0 ||
	    st.st_nlink != 1)
		return -1;
	return 0;
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
	if (access(AUTH_FINAL, F_OK) == 0)
	{
		fd = open(AUTH_FINAL, O_RDONLY);
		if (fd < 0)
			return 6;
		memset(buf, 0, sizeof(buf));
		n = read(fd, buf, sizeof(buf) - 1);
		close(fd);
		if (n != (ssize_t)strlen(AUTH_DATA) ||
		    memcmp(buf, AUTH_DATA, strlen(AUTH_DATA)) != 0)
			return 7;
		if (verify_large_file() != 0)
			return 15;
		if (verify_xauth_stat_contract() != 0)
			return 16;
		write_str("[EXT2] CLASSIFY EXT2_PERSISTENCE_OK\n");
		write_str("[EXT2] CLASSIFY EXT2_XAUTH_STAT_TIME_OK\n");
		write_str("[EXT2PERSISTOK]\n");
		return 0;
	}
	if (mkdir(TEST_DIR, 0700) != 0)
		return 8;
	fd = open(AUTH_TMP, O_CREAT | O_EXCL | O_WRONLY, 0600);
	if (fd < 0)
		return 9;
	if (write(fd, AUTH_DATA, strlen(AUTH_DATA)) != (ssize_t)strlen(AUTH_DATA))
		return 10;
	close(fd);
	if (link(AUTH_TMP, AUTH_FINAL) != 0)
		return 11;
	if (unlink(AUTH_TMP) != 0)
		return 12;
	fd = open(AUTH_FINAL, O_RDONLY);
	if (fd < 0)
		return 13;
	memset(buf, 0, sizeof(buf));
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n != (ssize_t)strlen(AUTH_DATA) ||
	    memcmp(buf, AUTH_DATA, strlen(AUTH_DATA)) != 0)
		return 14;
	if (create_large_file() != 0)
		return 15;
	if (verify_xauth_stat_contract() != 0)
		return 16;
	write_str("[EXT2] CLASSIFY EXT2_MOUNT_READ_OK\n");
	write_str("[EXT2] CLASSIFY EXT2_XAUTH_HARDLINK_OK\n");
	write_str("[EXT2] CLASSIFY EXT2_XAUTH_STAT_TIME_OK\n");
	write_str("[EXT2] CLASSIFY EXT2_SINGLE_INDIRECT_OK\n");
	write_str("[EXT2WRITEOK]\n");
	return 0;
}
