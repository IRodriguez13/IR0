/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: pty_multiplex_probe.c
 * Description: UNIX98 PTY multiplex audit — two ptmx masters, TIOCGPTN, pts/N I/O
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef _GNU_SOURCE
/* musl/glibc: TIOCGPTN / TIOCSPTLCK when headers omit them */
#ifndef TIOCGPTN
#define TIOCGPTN 0x80045430
#endif
#ifndef TIOCSPTLCK
#define TIOCSPTLCK 0x40045431
#endif
#endif

static void audit_pty(unsigned step, const char *op, long ret, int err)
{
	char buf[192];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "[LINUX_ABI_AUDIT][pty_multiplex] step=%u op=%s ret=%ld errno=%d\n",
		     step, op, ret, err);
	if (n > 0)
		(void)write(1, buf, (size_t)n);
}

static int open_pts(unsigned pty_num)
{
	char path[32];
	int fd;

	snprintf(path, sizeof(path), "/dev/pts/%u", pty_num);
	fd = open(path, O_RDWR | O_NOCTTY);
	return fd;
}

int main(void)
{
	int m0;
	int m1;
	int s0;
	int s1;
	unsigned n0;
	unsigned n1;
	int lock = 0;
	char ibuf[8];
	char obuf[8] = { 'A', 'B' };
	long ret;

	m0 = open("/dev/ptmx", O_RDWR | O_NOCTTY);
	audit_pty(0, "open_ptmx0", (long)m0, m0 < 0 ? errno : 0);
	if (m0 < 0)
		return 1;

	n0 = 9999;
	ret = ioctl(m0, TIOCGPTN, &n0);
	audit_pty(1, "tiocgptn0", (long)n0, ret != 0 ? errno : 0);
	if (ret != 0)
		return 1;

	ret = ioctl(m0, TIOCSPTLCK, &lock);
	audit_pty(2, "tiocsptlck0", ret, ret != 0 ? errno : 0);
	if (ret != 0)
		return 1;

	s0 = open_pts(n0);
	audit_pty(3, "open_pts0", (long)s0, s0 < 0 ? errno : 0);
	if (s0 < 0)
		return 1;

	m1 = open("/dev/ptmx", O_RDWR | O_NOCTTY);
	audit_pty(4, "open_ptmx1", (long)m1, m1 < 0 ? errno : 0);
	if (m1 < 0)
		return 1;

	n1 = 9999;
	ret = ioctl(m1, TIOCGPTN, &n1);
	audit_pty(5, "tiocgptn1", (long)n1, ret != 0 ? errno : 0);
	if (ret != 0 || n1 == n0)
		return 1;

	ret = ioctl(m1, TIOCSPTLCK, &lock);
	audit_pty(6, "tiocsptlck1", ret, ret != 0 ? errno : 0);
	if (ret != 0)
		return 1;

	s1 = open_pts(n1);
	audit_pty(7, "open_pts1", (long)s1, s1 < 0 ? errno : 0);
	if (s1 < 0)
		return 1;

	ret = (long)write(m0, obuf, 1);
	audit_pty(8, "write_m0", ret, ret < 0 ? errno : 0);
	if (ret != 1)
		return 1;

	memset(ibuf, 0, sizeof(ibuf));
	ret = (long)read(s0, ibuf, 1);
	audit_pty(9, "read_s0", ret, ret < 0 ? errno : 0);
	if (ret != 1 || ibuf[0] != 'A')
		return 1;

	ret = (long)write(m1, obuf + 1, 1);
	audit_pty(10, "write_m1", ret, ret < 0 ? errno : 0);
	if (ret != 1)
		return 1;

	memset(ibuf, 0, sizeof(ibuf));
	ret = (long)read(s1, ibuf, 1);
	audit_pty(11, "read_s1", ret, ret < 0 ? errno : 0);
	if (ret != 1 || ibuf[0] != 'B')
		return 1;

	close(s1);
	close(m1);
	close(s0);
	close(m0);

	(void)write(1, "[PTYMULTIPLEXOK]\n", 17);
	return 0;
}
