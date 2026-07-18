/**
 * IR0 userspace — KTM socketpair case
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_socketpair_case.c
 * Description: AF_UNIX SOCK_STREAM socketpair send/recv (X11 local prep).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "libktm_user.h"

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

static int test_socketpair(void)
{
	int sv[2];
	char buf[32];
	ssize_t n;
	const char *msg = "PAIROK";

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
		return -1;
	if (send(sv[0], msg, 6, 0) != 6)
		goto fail;
	n = recv(sv[1], buf, sizeof(buf), 0);
	if (n != 6 || memcmp(buf, msg, 6) != 0)
		goto fail;
	if (send(sv[1], "BACK", 4, 0) != 4)
		goto fail;
	n = recv(sv[0], buf, sizeof(buf), 0);
	if (n != 4 || memcmp(buf, "BACK", 4) != 0)
		goto fail;
	close(sv[0]);
	close(sv[1]);
	return 0;

fail:
	close(sv[0]);
	close(sv[1]);
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
		say("KTM_SOCKETPAIR_FAIL open\n");
		return 1;
	}
	if (ktm_get_caps(kfd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_SOCKETPAIR_FAIL caps\n");
		ktm_close(kfd);
		return 1;
	}

	(void)ktm_reset(kfd);
	if (ktm_case_begin(kfd, "socketpair") != 0)
	{
		say("KTM_SOCKETPAIR_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}

	if (test_socketpair() != 0)
	{
		(void)ktm_assert_true(kfd, "unix_socketpair", 0);
		fails++;
	}
	else
	{
		(void)ktm_assert_true(kfd, "unix_socketpair", 1);
		say("SOCKETPAIR_OK\n");
		say("KTM_SOCKETPAIR_OK\n");
	}

	(void)ktm_case_end(kfd, "socketpair", fails == 0 ? 0 : 1);
	ktm_close(kfd);
	if (fails == 0)
		say("KTM_USERDEV_OK\n");
	else
		say("KTM_USERDEV_FAIL\n");
	return fails == 0 ? 0 : 1;
}
