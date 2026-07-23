/**
 * IR0 userspace — KTM eventfd + timerfd case
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_event_fds_case.c
 * Description: eventfd2 write/read + timerfd one-shot.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include "libktm_user.h"

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

static int test_eventfd(void)
{
	int fd;
	uint64_t v = 7;
	uint64_t out = 0;
	ssize_t n;
	struct pollfd pfd;

	fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (fd < 0)
		return -1;
	pfd.fd = fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	if (poll(&pfd, 1, 0) != 0)
		goto fail;
	if (write(fd, &v, sizeof(v)) != (ssize_t)sizeof(v))
		goto fail;
	pfd.revents = 0;
	if (poll(&pfd, 1, 0) != 1 || !(pfd.revents & POLLIN))
		goto fail;
	n = read(fd, &out, sizeof(out));
	if (n != (ssize_t)sizeof(out) || out != 7)
		goto fail;
	close(fd);
	return 0;
fail:
	close(fd);
	return -1;
}

static int test_timerfd(void)
{
	int fd;
	struct itimerspec its;
	uint64_t exp = 0;
	ssize_t n;
	struct pollfd pfd;
	int i;

	fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	if (fd < 0)
		return -1;
	memset(&its, 0, sizeof(its));
	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 50 * 1000 * 1000; /* 50ms */
	if (timerfd_settime(fd, 0, &its, NULL) != 0)
		goto fail;
	pfd.fd = fd;
	pfd.events = POLLIN;
	for (i = 0; i < 40; i++)
	{
		pfd.revents = 0;
		if (poll(&pfd, 1, 20) == 1 && (pfd.revents & POLLIN))
			break;
	}
	if (!(pfd.revents & POLLIN))
		goto fail;
	n = read(fd, &exp, sizeof(exp));
	if (n != (ssize_t)sizeof(exp) || exp == 0)
		goto fail;
	close(fd);
	return 0;
fail:
	close(fd);
	return -1;
}

int main(void)
{
	int kfd;
	int fails = 0;
	ktm_user_caps_t caps;

	kfd = ktm_open();
	if (kfd < 0)
	{
		say("KTM_EVENT_FDS_FAIL open\n");
		return 1;
	}
	if (ktm_get_caps(kfd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_EVENT_FDS_FAIL caps\n");
		ktm_close(kfd);
		return 1;
	}
	(void)ktm_reset(kfd);
	if (ktm_case_begin(kfd, "event_fds") != 0)
	{
		say("KTM_EVENT_FDS_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}
	if (test_eventfd() != 0)
	{
		(void)ktm_assert_true(kfd, "eventfd", 0);
		fails++;
	}
	else
	{
		(void)ktm_assert_true(kfd, "eventfd", 1);
		say("EVENTFD_OK\n");
	}
	if (test_timerfd() != 0)
	{
		(void)ktm_assert_true(kfd, "timerfd", 0);
		fails++;
	}
	else
	{
		(void)ktm_assert_true(kfd, "timerfd", 1);
		say("TIMERFD_OK\n");
	}
	(void)ktm_case_end(kfd, "event_fds", fails == 0 ? 0 : 1);
	ktm_close(kfd);
	say(fails == 0 ? "KTM_USERDEV_OK\n" : "KTM_USERDEV_FAIL\n");
	return fails == 0 ? 0 : 1;
}
