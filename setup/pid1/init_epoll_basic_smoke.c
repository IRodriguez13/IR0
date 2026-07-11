/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: init_epoll_basic_smoke.c
 * Description: epoll_create1 + ctl + wait on a pipe.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <sys/epoll.h>
#include <unistd.h>
#include <string.h>

static void tag(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

int main(void)
{
	int fds[2];
	int ep;
	struct epoll_event ev;
	struct epoll_event out;
	int n;

	if (pipe(fds) != 0)
		return 2;
	ep = epoll_create1(0);
	if (ep < 0)
		return 3;
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN;
	ev.data.fd = fds[0];
	if (epoll_ctl(ep, EPOLL_CTL_ADD, fds[0], &ev) != 0)
		return 4;
	if (write(fds[1], "x", 1) != 1)
		return 5;
	n = epoll_wait(ep, &out, 1, 1000);
	if (n != 1 || !(out.events & EPOLLIN))
		return 6;
	tag("EPOLL_BASIC_OK\n");
	close(ep);
	close(fds[0]);
	close(fds[1]);
	return 0;
}
