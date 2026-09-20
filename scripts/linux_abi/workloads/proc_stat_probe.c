/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: proc_stat_probe.c
 * Description: /proc/stat + /proc/self/status workload for PROC-AUDIT compare.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PROC_STAT_PATH "/proc/stat"
#define PROC_SELF_STATUS "/proc/self/status"

static void audit_line(unsigned step, const char *op, long ret, int err)
{
	char buf[256];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "[LINUX_ABI_AUDIT][proc_stat] step=%u op=%s ret=%ld errno=%d\n",
		     step, op, ret, err);
	if (n > 0)
		(void)write(1, buf, (size_t)n);
}

static int read_cpu_jiffies(unsigned long long *user, unsigned long long *nice,
			    unsigned long long *system, unsigned long long *idle)
{
	char line[512];
	ssize_t n;
	int fd;

	fd = open(PROC_STAT_PATH, O_RDONLY);
	if (fd < 0)
		return -1;

	n = read(fd, line, sizeof(line) - 1);
	close(fd);
	if (n <= 0)
		return -1;

	line[n] = '\0';
	if (sscanf(line, "cpu %llu %llu %llu %llu",
		   user, nice, system, idle) != 4)
		return -1;

	return 0;
}

static long parse_self_uid(void)
{
	char buf[2048];
	char *p;
	long uid = -1;
	ssize_t n;
	int fd;

	fd = open(PROC_SELF_STATUS, O_RDONLY);
	if (fd < 0)
		return -1;

	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return -1;

	buf[n] = '\0';
	p = strstr(buf, "Uid:");
	if (!p)
		return -1;

	if (sscanf(p, "Uid:\t%ld", &uid) != 1)
		return -1;

	return uid;
}

int main(void)
{
	struct timespec ts = { .tv_sec = 2, .tv_nsec = 0 };
	unsigned long long user;
	unsigned long long nice;
	unsigned long long system;
	unsigned long long idle;
	long self_uid;
	long quiet;

	(void)nanosleep(&ts, NULL);

	if (read_cpu_jiffies(&user, &nice, &system, &idle) != 0)
	{
		audit_line(0, "read_stat", -1, errno);
		return 1;
	}

	audit_line(0, "cpu_user", (long)user, 0);
	audit_line(1, "cpu_system", (long)system, 0);
	audit_line(2, "cpu_idle", (long)idle, 0);

	quiet = (idle > user && idle > system) ? 1L : 0L;
	audit_line(3, "quiet_idle_dominant", quiet, 0);

	self_uid = parse_self_uid();
	audit_line(4, "self_uid", self_uid, self_uid < 0 ? ENOENT : 0);

	(void)write(1, "PROC_STATOK\n", 12);
	return quiet ? 0 : 1;
}
