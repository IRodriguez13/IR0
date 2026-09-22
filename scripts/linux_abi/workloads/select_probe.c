/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: select_probe.c
 * Description: select(2) pipe POLLIN / zero timeout / EINVAL ABI audit
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <stdio.h>
#include <sys/select.h>
#include <unistd.h>

static void audit_sel(unsigned step, const char *op, long ret, int err)
{
	char buf[160];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "[LINUX_ABI_AUDIT][select] step=%u op=%s ret=%ld errno=%d\n",
		     step, op, ret, err);
	if (n > 0)
		(void)write(1, buf, (size_t)n);
}

int main(void)
{
	int fds[2];
	unsigned char byte = 'x';
	fd_set rfds;
	struct timeval tv;
	int pr;

	if (pipe(fds) < 0)
		return 1;
	if (write(fds[1], &byte, 1) != 1)
		return 1;

	FD_ZERO(&rfds);
	FD_SET(fds[0], &rfds);
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	pr = select(fds[0] + 1, &rfds, NULL, NULL, &tv);
	audit_sel(0, "select_pipe", (long)pr, pr < 0 ? errno : 0);
	if (pr != 1 || !FD_ISSET(fds[0], &rfds))
	{
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	FD_ZERO(&rfds);
	FD_SET(fds[0], &rfds);
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	{
		unsigned char drain;

		if (read(fds[0], &drain, 1) != 1)
		{
			close(fds[0]);
			close(fds[1]);
			return 1;
		}
	}
	pr = select(fds[0] + 1, &rfds, NULL, NULL, &tv);
	audit_sel(1, "select_timeout0", (long)pr, pr < 0 ? errno : 0);
	if (pr != 0)
	{
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	pr = select(-1, NULL, NULL, NULL, NULL);
	audit_sel(2, "select_nfds_neg", (long)pr, pr < 0 ? errno : 0);
	if (pr >= 0 || errno != EINVAL)
	{
		close(fds[0]);
		close(fds[1]);
		return 1;
	}

	close(fds[0]);
	close(fds[1]);
	(void)write(1, "[SELECTOK]\n", 11);
	return 0;
}
