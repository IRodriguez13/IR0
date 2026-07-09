/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: poll_probe.c
 * Description: poll(2) on pipe POLLIN for Linux↔IR0 ABI audit
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void audit_poll(unsigned step, const char *op, long ret, int err,
		       unsigned revents)
{
	char buf[160];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "[LINUX_ABI_AUDIT][poll] step=%u op=%s ret=%ld errno=%d revents=%u\n",
		     step, op, ret, err, revents);
	if (n > 0)
		(void)write(1, buf, (size_t)n);
}

int main(void)
{
	int fds[2];
	struct pollfd pfd;
	unsigned char byte = 'x';
	int pr;

	if (pipe(fds) < 0)
		return 1;
	if (write(fds[1], &byte, 1) != 1)
		return 1;

	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = fds[0];
	pfd.events = POLLIN;
	pr = poll(&pfd, 1, 1000);
	audit_poll(0, "poll_pipe", (long)pr, pr < 0 ? errno : 0,
		   pr > 0 ? (unsigned)pfd.revents : 0U);
	close(fds[0]);
	close(fds[1]);

	if (pr != 1 || !(pfd.revents & POLLIN))
		return 1;

	(void)write(1, "[POLLOK]\n", 9);
	return 0;
}
