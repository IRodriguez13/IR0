/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: stat_probe.c
 * Description: stat(2)/fstat(2) workload for Linux↔IR0 ABI audit
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#define STAT_PROC_PATH "/proc/uptime"
#define STAT_FILE_PATH "/sbin/init"
#define STAT_NOENT_PATH "/sbin/noent"

static void audit_stat(unsigned step, const char *op, long ret, int err,
		       unsigned mode, int64_t size, unsigned long nlink)
{
	char buf[320];
	int n;

	if (ret == 0)
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][stat] step=%u op=%s ret=%ld errno=%d "
			     "mode=0%o size=%lld nlink=%lu\n",
			     step, op, ret, err, mode, (long long)size, nlink);
	}
	else
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][stat] step=%u op=%s ret=%ld errno=%d\n",
			     step, op, ret, err);
	}
	if (n > 0)
		(void)write(1, buf, (size_t)n);
}

int main(void)
{
	struct stat st;
	long ret;
	int fd;

	if (stat(STAT_PROC_PATH, &st) < 0)
	{
		audit_stat(0, "stat_proc", -1, errno, 0, 0, 0);
		return 1;
	}
	audit_stat(0, "stat_proc", 0, 0, (unsigned)st.st_mode, (int64_t)st.st_size,
		   (unsigned long)st.st_nlink);

	if (stat(STAT_FILE_PATH, &st) < 0)
	{
		audit_stat(1, "stat_file", -1, errno, 0, 0, 0);
		return 1;
	}
	audit_stat(1, "stat_file", 0, 0, (unsigned)st.st_mode, (int64_t)st.st_size,
		   (unsigned long)st.st_nlink);

	if (stat(STAT_NOENT_PATH, &st) == 0)
		return 1;
	audit_stat(2, "stat_noent", -1, errno, 0, 0, 0);
	if (errno != ENOENT)
		return 1;

	fd = open(STAT_PROC_PATH, O_RDONLY);
	if (fd < 0)
		return 1;

	if (fstat(fd, &st) < 0)
	{
		audit_stat(3, "fstat_proc", -1, errno, 0, 0, 0);
		close(fd);
		return 1;
	}
	audit_stat(3, "fstat_proc", 0, 0, (unsigned)st.st_mode, (int64_t)st.st_size,
		   (unsigned long)st.st_nlink);
	close(fd);

	if (fstat(-1, &st) == 0)
		return 1;
	audit_stat(4, "fstat_ebadf", -1, errno, 0, 0, 0);
	if (errno != EBADF)
		return 1;

	(void)write(1, "[STATOK]\n", 9);
	return 0;
}
