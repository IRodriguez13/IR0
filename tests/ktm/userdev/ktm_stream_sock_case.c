/**
 * IR0 userspace — KTM stream sock case
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_stream_sock_case.c
 * Description: AF_UNIX + TCP loopback stream (canonical vs smoke-stream-sock).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "libktm_user.h"

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

static int test_unix(void)
{
	int srv, cli, acc;
	struct sockaddr_un addr;
	char buf[32];
	ssize_t n;
	const char *msg = "UNIXOK";

	unlink("/tmp/ir0.sock");
	srv = socket(AF_UNIX, SOCK_STREAM, 0);
	if (srv < 0)
		return -1;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, "/tmp/ir0.sock", sizeof(addr.sun_path) - 1);
	if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0)
		goto fail_srv;
	if (listen(srv, 1) != 0)
		goto fail_srv;
	cli = socket(AF_UNIX, SOCK_STREAM, 0);
	if (cli < 0)
		goto fail_srv;
	if (connect(cli, (struct sockaddr *)&addr, sizeof(addr)) != 0)
		goto fail_cli;
	acc = accept(srv, NULL, NULL);
	if (acc < 0)
		goto fail_cli;
	if (send(cli, msg, 6, 0) != 6)
		goto fail_acc;
	n = recv(acc, buf, sizeof(buf), 0);
	if (n != 6 || memcmp(buf, msg, 6) != 0)
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

static int test_tcp(void)
{
	int srv, cli, acc;
	struct sockaddr_in addr;
	char buf[32];
	ssize_t n;
	const char *msg = "TCPOK";

	srv = socket(AF_INET, SOCK_STREAM, 0);
	if (srv < 0)
		return -1;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(7777);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0)
		goto fail_srv;
	if (listen(srv, 1) != 0)
		goto fail_srv;
	cli = socket(AF_INET, SOCK_STREAM, 0);
	if (cli < 0)
		goto fail_srv;
	if (connect(cli, (struct sockaddr *)&addr, sizeof(addr)) != 0)
		goto fail_cli;
	acc = accept(srv, NULL, NULL);
	if (acc < 0)
		goto fail_cli;
	if (send(cli, msg, 5, 0) != 5)
		goto fail_acc;
	n = recv(acc, buf, sizeof(buf), 0);
	if (n != 5 || memcmp(buf, msg, 5) != 0)
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

int main(void)
{
	int kfd;
	int fails = 0;
	ktm_user_caps_t caps;

	kfd = ktm_open();
	if (kfd < 0)
	{
		say("KTM_STREAM_SOCK_FAIL open\n");
		return 1;
	}
	if (ktm_get_caps(kfd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_STREAM_SOCK_FAIL caps\n");
		ktm_close(kfd);
		return 1;
	}

	(void)ktm_reset(kfd);
	if (ktm_case_begin(kfd, "stream_sock") != 0)
	{
		say("KTM_STREAM_SOCK_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}

	if (test_unix() != 0)
	{
		(void)ktm_assert_true(kfd, "unix_stream", 0);
		fails++;
	}
	else
	{
		(void)ktm_assert_true(kfd, "unix_stream", 1);
	}

	if (test_tcp() != 0)
	{
		(void)ktm_assert_true(kfd, "tcp_loopback", 0);
		fails++;
	}
	else
	{
		(void)ktm_assert_true(kfd, "tcp_loopback", 1);
	}

	if (fails == 0)
	{
		say("STREAM_SOCK_OK\n");
		say("STREAM_SENDRECV_OK\n");
		say("KTM_STREAM_SOCK_OK\n");
	}

	(void)ktm_case_end(kfd, "stream_sock", fails == 0 ? 0 : 1);
	ktm_close(kfd);
	if (fails == 0)
		say("KTM_USERDEV_OK\n");
	else
		say("KTM_USERDEV_FAIL\n");
	return fails == 0 ? 0 : 1;
}
