/**
 * IR0 userspace — KTM epoll basic case
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_epoll_case.c
 * Description: epoll_create1 + ctl + wait on a pipe (canonical vs smoke-epoll-basic).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "libktm_user.h"

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

int main(void)
{
	int kfd;
	int fds[2];
	int ep;
	struct epoll_event ev;
	struct epoll_event out;
	int n;
	int fails = 0;
	ktm_user_caps_t caps;

	kfd = ktm_open();
	if (kfd < 0)
	{
		say("KTM_EPOLL_FAIL open\n");
		return 1;
	}
	if (ktm_get_caps(kfd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_EPOLL_FAIL caps\n");
		ktm_close(kfd);
		return 1;
	}

	(void)ktm_reset(kfd);
	if (ktm_case_begin(kfd, "epoll_basic") != 0)
	{
		say("KTM_EPOLL_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}

	if (pipe(fds) != 0)
	{
		(void)ktm_assert_true(kfd, "pipe_ok", 0);
		fails++;
		goto done;
	}
	(void)ktm_assert_true(kfd, "pipe_ok", 1);

	ep = epoll_create1(0);
	if (ep < 0)
	{
		(void)ktm_assert_true(kfd, "epoll_create", 0);
		fails++;
		close(fds[0]);
		close(fds[1]);
		goto done;
	}
	(void)ktm_assert_true(kfd, "epoll_create", 1);

	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN;
	ev.data.fd = fds[0];
	if (epoll_ctl(ep, EPOLL_CTL_ADD, fds[0], &ev) != 0)
	{
		(void)ktm_assert_true(kfd, "epoll_ctl", 0);
		fails++;
		close(ep);
		close(fds[0]);
		close(fds[1]);
		goto done;
	}
	(void)ktm_assert_true(kfd, "epoll_ctl", 1);

	if (write(fds[1], "x", 1) != 1)
	{
		(void)ktm_assert_true(kfd, "pipe_write", 0);
		fails++;
		close(ep);
		close(fds[0]);
		close(fds[1]);
		goto done;
	}

	n = epoll_wait(ep, &out, 1, 1000);
	if (n != 1 || !(out.events & EPOLLIN))
	{
		(void)ktm_assert_true(kfd, "epoll_wait", 0);
		fails++;
	}
	else
	{
		(void)ktm_assert_true(kfd, "epoll_wait", 1);
		say("EPOLL_BASIC_OK\n");
		say("KTM_EPOLL_OK\n");
	}

	close(ep);
	close(fds[0]);
	close(fds[1]);

done:
	(void)ktm_case_end(kfd, "epoll_basic", fails == 0 ? 0 : 1);
	ktm_close(kfd);
	if (fails == 0)
		say("KTM_USERDEV_OK\n");
	else
		say("KTM_USERDEV_FAIL\n");
	return fails == 0 ? 0 : 1;
}
