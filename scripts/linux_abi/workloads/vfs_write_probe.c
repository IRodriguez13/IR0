/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: vfs_write_probe.c
 * Description: VFS write-path bundle workload (open/write/lseek/unlink/rename/mkdir/truncate)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef VFS_WRITE_ROOT
#define VFS_WRITE_ROOT "/tmp"
#endif

#ifndef VFS_WRITE_WD
#define VFS_WRITE_WD VFS_WRITE_ROOT "/ir0wtest"
#endif

#define HELLO "hello\n"
#define HELLO_LEN 6U
#define HELLO_HEX "68656c6c6f0a"

static unsigned g_step;

static void audit_step(const char *op, long ret, int err, const char *data_hex,
		       long stat_size)
{
	char buf[384];
	int n;

	g_step++;
	if (data_hex && stat_size >= 0)
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][vfs_write] step=%u op=%s ret=%ld errno=%d "
			     "data_hex=%s stat_size=%ld\n",
			     g_step, op, ret, err, data_hex, stat_size);
	}
	else if (data_hex)
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][vfs_write] step=%u op=%s ret=%ld errno=%d "
			     "data_hex=%s\n",
			     g_step, op, ret, err, data_hex);
	}
	else if (stat_size >= 0)
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][vfs_write] step=%u op=%s ret=%ld errno=%d "
			     "stat_size=%ld\n",
			     g_step, op, ret, err, stat_size);
	}
	else
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][vfs_write] step=%u op=%s ret=%ld errno=%d\n",
			     g_step, op, ret, err);
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

static int ensure_workdir(void)
{
	if (access(VFS_WRITE_ROOT, W_OK) != 0)
	{
		if (mkdir(VFS_WRITE_ROOT, 0777) != 0 && errno != EEXIST)
			return -1;
		if (access(VFS_WRITE_ROOT, W_OK) != 0)
		{
			if (mount("none", VFS_WRITE_ROOT, "tmpfs", 0, NULL) != 0)
				return -1;
		}
	}
	if (mkdir(VFS_WRITE_WD, 0755) != 0 && errno != EEXIST)
		return -1;
	return 0;
}

static void cleanup_paths(void)
{
	(void)unlink(VFS_WRITE_WD "/a");
	(void)unlink(VFS_WRITE_WD "/u");
	(void)unlink(VFS_WRITE_WD "/r1");
	(void)unlink(VFS_WRITE_WD "/r2");
	(void)unlink(VFS_WRITE_WD "/r3");
	(void)unlink(VFS_WRITE_WD "/r4");
	(void)unlink(VFS_WRITE_WD "/tfile");
	(void)unlink(VFS_WRITE_WD "/nd");
	(void)unlink(VFS_WRITE_WD "/d");
	(void)unlink(VFS_WRITE_WD "/child");
	(void)unlink(VFS_WRITE_WD "/parent");
	(void)rmdir(VFS_WRITE_WD "/child");
	(void)rmdir(VFS_WRITE_WD "/parent");
	(void)rmdir(VFS_WRITE_WD "/d");
	(void)rmdir(VFS_WRITE_WD "/nd");
}

int main(void)
{
	char path[256];
	char hex[128];
	char rbuf[64];
	int fd;
	long ret;
	ssize_t n;
	struct stat st;

	g_step = 0U;
	cleanup_paths();
	if (ensure_workdir() != 0)
	{
		audit_step("setup_wd", -1, errno, NULL, -1);
		return 1;
	}
	audit_step("setup_wd", 0, 0, NULL, -1);

	snprintf(path, sizeof(path), "%s/a", VFS_WRITE_WD);
	fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0644);
	audit_step("open_creat", (long)fd, fd < 0 ? errno : 0, NULL, -1);
	if (fd < 0)
		return 1;
	close(fd);

	errno = 0;
	fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0644);
	audit_step("open_excl_eexist", (long)fd, errno, NULL, -1);
	if (fd != -1 || errno != EEXIST)
		return 1;

	fd = open(path, O_TRUNC | O_WRONLY);
	audit_step("open_trunc", (long)fd, fd < 0 ? errno : 0, NULL, -1);
	if (fd < 0)
		return 1;

	n = write(fd, HELLO, HELLO_LEN);
	audit_step("write_hello", (long)n, n < 0 ? errno : 0, NULL, -1);
	if (n != (ssize_t)HELLO_LEN)
	{
		close(fd);
		return 1;
	}
	close(fd);

	fd = open(path, O_RDONLY);
	audit_step("open_read", (long)fd, fd < 0 ? errno : 0, NULL, -1);
	if (fd < 0)
		return 1;

	memset(rbuf, 0, sizeof(rbuf));
	n = read(fd, rbuf, sizeof(rbuf) - 1U);
	if (n == (ssize_t)HELLO_LEN && hex_encode((unsigned char *)rbuf, HELLO_LEN,
						  hex, sizeof(hex)) == 0)
		audit_step("read_data", (long)n, 0, hex, -1);
	else
		audit_step("read_data", (long)n, n < 0 ? errno : 0, NULL, -1);
	if (n != (ssize_t)HELLO_LEN || memcmp(rbuf, HELLO, HELLO_LEN) != 0)
	{
		close(fd);
		return 1;
	}

	n = read(fd, rbuf, sizeof(rbuf));
	audit_step("read_eof", (long)n, n < 0 ? errno : 0, NULL, -1);
	if (n != 0)
	{
		close(fd);
		return 1;
	}
	close(fd);

	fd = open(path, O_APPEND | O_WRONLY);
	audit_step("open_append", (long)fd, fd < 0 ? errno : 0, NULL, -1);
	if (fd < 0)
		return 1;
	n = write(fd, "!", 1);
	audit_step("write_append", (long)n, n < 0 ? errno : 0, NULL, -1);
	if (n != 1)
	{
		close(fd);
		return 1;
	}
	close(fd);

	if (stat(path, &st) == 0)
		audit_step("stat_after_append", 0, 0, NULL, (long)st.st_size);
	else
		audit_step("stat_after_append", -1, errno, NULL, -1);
	if (stat(path, &st) != 0 || st.st_size != 7)
		return 1;

	fd = open(path, O_RDWR);
	audit_step("open_lseek", (long)fd, fd < 0 ? errno : 0, NULL, -1);
	if (fd < 0)
		return 1;

	ret = lseek(fd, 0, SEEK_SET);
	audit_step("lseek_set", ret, ret < 0 ? errno : 0, NULL, -1);
	if (ret != 0)
	{
		close(fd);
		return 1;
	}

	memset(rbuf, 0, sizeof(rbuf));
	n = read(fd, rbuf, 1);
	if (n == 1 && hex_encode((unsigned char *)rbuf, 1, hex, sizeof(hex)) == 0)
		audit_step("read_after_lseek_set", (long)n, 0, hex, -1);
	else
		audit_step("read_after_lseek_set", (long)n, n < 0 ? errno : 0, NULL, -1);
	if (n != 1 || rbuf[0] != 'h')
	{
		close(fd);
		return 1;
	}

	ret = lseek(fd, 5, SEEK_CUR);
	audit_step("lseek_cur", ret, ret < 0 ? errno : 0, NULL, -1);
	if (ret != 6)
	{
		close(fd);
		return 1;
	}

	ret = lseek(fd, -1, SEEK_END);
	audit_step("lseek_end", ret, ret < 0 ? errno : 0, NULL, -1);
	if (ret != 6)
	{
		close(fd);
		return 1;
	}
	close(fd);

	snprintf(path, sizeof(path), "%s/u", VFS_WRITE_WD);
	fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (fd >= 0)
	{
		(void)write(fd, "x", 1);
		close(fd);
	}
	ret = unlink(path);
	audit_step("unlink_ok", ret, ret < 0 ? errno : 0, NULL, -1);
	if (ret != 0)
		return 1;

	errno = 0;
	ret = unlink(path);
	audit_step("unlink_noent", ret, errno, NULL, -1);
	if (ret != -1 || errno != ENOENT)
		return 1;

	snprintf(path, sizeof(path), "%s/d", VFS_WRITE_WD);
	if (mkdir(path, 0755) != 0 && errno != EEXIST)
		return 1;
	errno = 0;
	ret = unlink(path);
	audit_step("unlink_dir", ret, errno, NULL, -1);
	if (ret != -1 || errno != EISDIR)
		return 1;
	(void)rmdir(path);

	snprintf(path, sizeof(path), "%s/r1", VFS_WRITE_WD);
	fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (fd < 0)
		return 1;
	(void)write(fd, "1", 1);
	close(fd);
	snprintf(path, sizeof(path), "%s/r2", VFS_WRITE_WD);
	ret = rename(VFS_WRITE_WD "/r1", path);
	audit_step("rename_ok", ret, ret < 0 ? errno : 0, NULL, -1);
	if (ret != 0)
		return 1;

	fd = open(VFS_WRITE_WD "/r3", O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (fd < 0)
		return 1;
	(void)write(fd, "3", 1);
	close(fd);
	fd = open(VFS_WRITE_WD "/r4", O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (fd < 0)
		return 1;
	(void)write(fd, "4", 1);
	close(fd);
	ret = rename(VFS_WRITE_WD "/r3", VFS_WRITE_WD "/r4");
	audit_step("rename_overwrite", ret, ret < 0 ? errno : 0, NULL, -1);
	if (ret != 0)
		return 1;

	errno = 0;
	ret = rename(VFS_WRITE_WD "/noent_a", VFS_WRITE_WD "/noent_b");
	audit_step("rename_noent", ret, errno, NULL, -1);
	if (ret != -1 || errno != ENOENT)
		return 1;

	snprintf(path, sizeof(path), "%s/nd", VFS_WRITE_WD);
	ret = mkdir(path, 0755);
	audit_step("mkdir_ok", ret, ret < 0 ? errno : 0, NULL, -1);
	if (ret != 0)
		return 1;

	errno = 0;
	ret = mkdir(path, 0755);
	audit_step("mkdir_eexist", ret, errno, NULL, -1);
	if (ret != -1 || errno != EEXIST)
		return 1;

	ret = rmdir(path);
	audit_step("rmdir_empty", ret, ret < 0 ? errno : 0, NULL, -1);
	if (ret != 0)
		return 1;

	if (mkdir(VFS_WRITE_WD "/parent", 0755) != 0)
		return 1;
	if (mkdir(VFS_WRITE_WD "/parent/child", 0755) != 0)
		return 1;
	errno = 0;
	ret = rmdir(VFS_WRITE_WD "/parent");
	audit_step("rmdir_nonempty", ret, errno, NULL, -1);
	if (ret != -1 || errno != ENOTEMPTY)
		return 1;
	(void)rmdir(VFS_WRITE_WD "/parent/child");
	(void)rmdir(VFS_WRITE_WD "/parent");

	snprintf(path, sizeof(path), "%s/tfile", VFS_WRITE_WD);
	fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (fd < 0)
		return 1;
	(void)write(fd, "0123456789", 10);
	close(fd);

	ret = truncate(path, 3);
	audit_step("truncate_shrink", ret, ret < 0 ? errno : 0, NULL, -1);
	if (ret != 0)
		return 1;

	errno = 0;
	ret = truncate(path, 100);
	audit_step("truncate_grow", ret, errno, NULL, -1);
	/* IR0/MINIX may return ENOSYS; comparator marks optional. */
	if (ret != 0 && errno != ENOSYS)
		return 1;

	fd = open(path, O_RDWR);
	if (fd >= 0)
	{
		errno = 0;
		ret = ftruncate(fd, 2);
		audit_step("ftruncate_shrink", ret, ret < 0 ? errno : 0, NULL, -1);
		close(fd);
	}
	else
	{
		audit_step("ftruncate_shrink", -1, errno, NULL, -1);
	}

	(void)write(1, "[VFSWRITEOK]\n", 13);
	return 0;
}
