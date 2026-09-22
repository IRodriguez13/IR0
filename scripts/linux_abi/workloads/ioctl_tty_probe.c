/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ioctl_tty_probe.c
 * Description: Console TTY ioctl(TCGETS/TIOCGWINSZ/TIOCGPGRP) ABI audit probe
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static void audit_io(unsigned step, const char *op, long ret, int err)
{
	char buf[160];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "[LINUX_ABI_AUDIT][ioctl] step=%u op=%s ret=%ld errno=%d\n",
		     step, op, ret, err);
	if (n > 0)
		(void)write(1, buf, (size_t)n);
}

int main(void)
{
	struct termios t, saved;
	struct winsize ws;
	pid_t pgrp;
	unsigned char *raw;
	size_t i;
	int ret;

	/*
	 * Linux x86-64 TCGETS writes struct __kernel_termios (36 bytes,
	 * NCCS=19) into musl's larger struct termios.  The tail must remain
	 * untouched; musl tcgetattr() is a direct ioctl wrapper.
	 */
	memset(&t, 0xa5, sizeof(t));
	ret = ioctl(0, TCGETS, &t);
	audit_io(0, "tcgets", ret, ret != 0 ? errno : 0);
	if (ret != 0)
		return 1;
	raw = (unsigned char *)&t;
	for (i = 36; i < sizeof(t); i++)
	{
		if (raw[i] != 0xa5)
			return 2;
	}
	audit_io(1, "tcgets_tail_untouched", 0, 0);

	saved = t;
	t.c_lflag &= (tcflag_t)~(ECHO | ECHOE | ECHOK);
	ret = tcsetattr(0, TCSANOW, &t);
	audit_io(2, "tcsetattr_noecho", ret, ret != 0 ? errno : 0);
	if (ret != 0)
		return 3;
	memset(&t, 0, sizeof(t));
	ret = tcgetattr(0, &t);
	audit_io(3, "tcgetattr_verify", ret, ret != 0 ? errno : 0);
	if (ret != 0 || (t.c_lflag & ECHO) != 0)
		return 4;
	ret = tcsetattr(0, TCSANOW, &saved);
	audit_io(4, "tcsetattr_restore", ret, ret != 0 ? errno : 0);
	if (ret != 0)
		return 5;

	memset(&ws, 0, sizeof(ws));
	ret = ioctl(0, TIOCGWINSZ, &ws);
	audit_io(5, "tiocgwinsz", ret, ret != 0 ? errno : 0);
	if (ret != 0)
		return 6;
	if (ws.ws_row == 0 || ws.ws_col == 0)
		return 7;

	ret = ioctl(0, TIOCGPGRP, &pgrp);
	audit_io(6, "tiocgpgrp", ret, ret != 0 ? errno : 0);
	if (ret != 0 || pgrp <= 0)
		return 8;

	(void)write(1, "[IOCTLTTYOK]\n", 13);
	return 0;
}
