/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_vfs_backend_contract.c
 * Description: Host-side mock backend exercising vfs_ops I/O contract
 *              (docs/fs-vfs-contract.md). Does not link fs/vfs.c.
 */

#include "test_harness_ir0.h"
#include <ir0/vfs_backend.h>
#include <ir0/errno.h>
#include <ir0/stat.h>
#include <string.h>

#define MOCK_PATH "/mockfile"
#define MOCK_CAP  4096

typedef struct
{
	char data[MOCK_CAP];
	size_t size;
	mode_t mode;
} mock_file_t;

static mock_file_t mock_file;
static int mock_stat_fresh;

static int mock_stat(const char *path, stat_t *buf)
{
	if (!path || !buf)
		return -EINVAL;
	if (strcmp(path, MOCK_PATH) != 0)
		return -ENOENT;

	buf->st_mode = mock_file.mode;
	buf->st_size = (off_t)mock_file.size;
	buf->st_ino = 1;
	mock_stat_fresh = 1;
	return 0;
}

static int mock_read(const char *path, void *buf, size_t count,
		     size_t *bytes_read, off_t offset)
{
	size_t n;

	if (!path || !buf)
		return -EINVAL;
	if (offset < 0)
		return -EINVAL;
	if (strcmp(path, MOCK_PATH) != 0)
		return -ENOENT;
	if (S_ISDIR(mock_file.mode))
		return -EISDIR;
	if ((size_t)offset >= mock_file.size)
	{
		if (bytes_read)
			*bytes_read = 0;
		return 0;
	}

	n = mock_file.size - (size_t)offset;
	if (n > count)
		n = count;
	memcpy(buf, mock_file.data + (size_t)offset, n);
	if (bytes_read)
		*bytes_read = n;
	return 0;
}

static int mock_write(const char *path, const void *buf, size_t count,
		      size_t *bytes_written, off_t offset)
{
	size_t end;
	size_t i;

	if (!path || !buf)
		return -EINVAL;
	if (offset < 0)
		return -EINVAL;
	if (strcmp(path, MOCK_PATH) != 0)
		return -ENOENT;
	if (S_ISDIR(mock_file.mode))
		return -EISDIR;

	end = (size_t)offset + count;
	if (end > MOCK_CAP)
		return -EFBIG;

	if (end > mock_file.size)
	{
		for (i = mock_file.size; i < (size_t)offset; i++)
			mock_file.data[i] = 0;
		mock_file.size = end;
	}

	memcpy(mock_file.data + (size_t)offset, buf, count);
	if (bytes_written)
		*bytes_written = count;
	mock_stat_fresh = 0;
	return 0;
}

static int mock_truncate(const char *path, size_t length)
{
	if (!path)
		return -EINVAL;
	if (strcmp(path, MOCK_PATH) != 0)
		return -ENOENT;
	if (S_ISDIR(mock_file.mode))
		return -EISDIR;
	if (length > MOCK_CAP)
		return -EFBIG;
	if (length > mock_file.size)
		return -ENOSYS;

	mock_file.size = length;
	mock_stat_fresh = 0;
	return 0;
}

static struct vfs_ops mock_ops = {
	.stat = mock_stat,
	.read = mock_read,
	.write = mock_write,
	.truncate = mock_truncate,
};

static void mock_reset(void)
{
	memset(&mock_file, 0, sizeof(mock_file));
	mock_file.mode = S_IFREG | 0644;
	memcpy(mock_file.data, "hello", 5);
	mock_file.size = 5;
	mock_stat_fresh = 0;
}

void test_vfs_backend_contract(void)
{
	char buf[32];
	size_t n;
	stat_t st;
	int rc;

	TEST_BEGIN("vfs_backend_contract_pread_eof");

	mock_reset();

	n = 999;
	rc = mock_ops.read(MOCK_PATH, buf, sizeof(buf), &n, 0);
	ASSERT_EQ(rc, 0);
	ASSERT_EQ(n, 5);
	ASSERT_STR_EQ(buf, "hello");

	n = 999;
	rc = mock_ops.read(MOCK_PATH, buf, 4, &n, 2);
	ASSERT_EQ(rc, 0);
	ASSERT_EQ(n, 3);
	ASSERT(memcmp(buf, "llo", 3) == 0);

	n = 999;
	rc = mock_ops.read(MOCK_PATH, buf, 4, &n, 100);
	ASSERT_EQ(rc, 0);
	ASSERT_EQ(n, 0);

	TEST_END();

	TEST_BEGIN("vfs_backend_contract_pwrite_hole");

	mock_reset();

	n = 0;
	rc = mock_ops.write(MOCK_PATH, "X", 1, &n, 10);
	ASSERT_EQ(rc, 0);
	ASSERT_EQ(n, 1);
	ASSERT_EQ(mock_file.size, 11);
	ASSERT_EQ(mock_file.data[10], 'X');
	ASSERT_EQ(mock_file.data[5], 0);

	TEST_END();

	TEST_BEGIN("vfs_backend_contract_truncate_stat");

	mock_reset();
	mock_stat_fresh = 1;

	rc = mock_ops.truncate(MOCK_PATH, 3);
	ASSERT_EQ(rc, 0);
	ASSERT_EQ(mock_file.size, 3);

	rc = mock_ops.stat(MOCK_PATH, &st);
	ASSERT_EQ(rc, 0);
	ASSERT_EQ(st.st_size, 3);
	ASSERT(mock_stat_fresh);

	rc = mock_ops.truncate(MOCK_PATH, 10);
	ASSERT_EQ(rc, -ENOSYS);

	TEST_END();

	fprintf(stderr, "[TEST] CLASSIFY VFS_FS_CONTRACT_DOCUMENTED\n");
}
