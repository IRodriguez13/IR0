/**
 * IR0 userspace — KTM abstract unix + poll case
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_unix_abstract_case.c
 * Description: abstract @unix bind/connect + poll on socketpair.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stddef.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "libktm_user.h"

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

static int test_abstract(void)
{
	int srv, cli, acc;
	struct sockaddr_un addr;
	char buf[16];
	ssize_t n;
	const char *msg = "ABS";

	srv = socket(AF_UNIX, SOCK_STREAM, 0);
	if (srv < 0)
		return -1;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	addr.sun_path[0] = '\0';
	memcpy(addr.sun_path + 1, "ir0abs", 6);
	if (bind(srv, (struct sockaddr *)&addr, offsetof(struct sockaddr_un, sun_path) + 7) != 0)
		goto fail_srv;
	if (listen(srv, 1) != 0)
		goto fail_srv;
	cli = socket(AF_UNIX, SOCK_STREAM, 0);
	if (cli < 0)
		goto fail_srv;
	if (connect(cli, (struct sockaddr *)&addr, offsetof(struct sockaddr_un, sun_path) + 7) != 0)
		goto fail_cli;
	acc = accept(srv, NULL, NULL);
	if (acc < 0)
		goto fail_cli;
	if (send(cli, msg, 3, 0) != 3)
		goto fail_acc;
	n = recv(acc, buf, sizeof(buf), 0);
	if (n != 3 || memcmp(buf, msg, 3) != 0)
		goto fail_acc;
	close(acc);
	close(cli);
	close(srv);
	return 0;

fail_acc:
	close(acc);
fail_cli:
	close(cli);
fail_srv:
	close(srv);
	return -1;
}

static int test_poll_pair(void)
{
	int sv[2];
	struct pollfd pfd;
	char buf[8];

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
		return -1;
	pfd.fd = sv[1];
	pfd.events = POLLIN;
	pfd.revents = 0;
	if (poll(&pfd, 1, 0) != 0)
		goto fail;
	if (send(sv[0], "P", 1, 0) != 1)
		goto fail;
	pfd.revents = 0;
	if (poll(&pfd, 1, 100) != 1 || !(pfd.revents & POLLIN))
		goto fail;
	if (recv(sv[1], buf, 1, 0) != 1 || buf[0] != 'P')
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
		say("KTM_UNIX_ABSTRACT_FAIL open\n");
		return 1;
	}
	if (ktm_get_caps(kfd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_UNIX_ABSTRACT_FAIL caps\n");
		ktm_close(kfd);
		return 1;
	}
	(void)ktm_reset(kfd);
	if (ktm_case_begin(kfd, "unix_abstract") != 0)
	{
		say("KTM_UNIX_ABSTRACT_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}
	if (test_abstract() != 0)
	{
		(void)ktm_assert_true(kfd, "abstract", 0);
		fails++;
	}
	else
		(void)ktm_assert_true(kfd, "abstract", 1);
	if (test_poll_pair() != 0)
	{
		(void)ktm_assert_true(kfd, "sock_poll", 0);
		fails++;
	}
	else
		(void)ktm_assert_true(kfd, "sock_poll", 1);
	if (fails == 0)
	{
		say("UNIX_ABSTRACT_OK\n");
		say("SOCK_POLL_OK\n");
		say("KTM_UNIX_ABSTRACT_OK\n");
	}
	(void)ktm_case_end(kfd, "unix_abstract", fails == 0 ? 0 : 1);
	ktm_close(kfd);
	say(fails == 0 ? "KTM_USERDEV_OK\n" : "KTM_USERDEV_FAIL\n");
	return fails == 0 ? 0 : 1;
}
