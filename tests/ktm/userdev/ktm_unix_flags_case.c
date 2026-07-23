/**
 * IR0 userspace — KTM unix flags case
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_unix_flags_case.c
 * Description: SOCK_CLOEXEC/NONBLOCK, accept4, MSG_PEEK, SO_REUSEADDR.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>

#include "libktm_user.h"

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 02000000
#endif
#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 04000
#endif
#ifndef MSG_PEEK
#define MSG_PEEK 0x2
#endif

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

static int test_sock_flags(void)
{
	int fd;
	int fl;

	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0)
		return -1;
	fl = fcntl(fd, F_GETFD);
	if (fl < 0 || !(fl & FD_CLOEXEC))
		goto fail;
	fl = fcntl(fd, F_GETFL);
	if (fl < 0 || !(fl & O_NONBLOCK))
		goto fail;
	close(fd);
	return 0;
fail:
	close(fd);
	return -1;
}

static int test_accept4(void)
{
	int srv, cli, acc;
	struct sockaddr_un addr;
	int fl;
	const char name[] = "ir0a4";

	srv = socket(AF_UNIX, SOCK_STREAM, 0);
	if (srv < 0)
		return -1;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	addr.sun_path[0] = '\0';
	memcpy(addr.sun_path + 1, name, sizeof(name) - 1);
	if (bind(srv, (struct sockaddr *)&addr,
		 offsetof(struct sockaddr_un, sun_path) + 1 + sizeof(name) - 1) != 0)
		goto fail_srv;
	if (listen(srv, 1) != 0)
		goto fail_srv;
	cli = socket(AF_UNIX, SOCK_STREAM, 0);
	if (cli < 0)
		goto fail_srv;
	if (connect(cli, (struct sockaddr *)&addr,
		    offsetof(struct sockaddr_un, sun_path) + 1 + sizeof(name) - 1) != 0)
		goto fail_cli;
	acc = (int)syscall(SYS_accept4, srv, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
	if (acc < 0)
		goto fail_cli;
	fl = fcntl(acc, F_GETFD);
	if (fl < 0 || !(fl & FD_CLOEXEC))
		goto fail_acc;
	fl = fcntl(acc, F_GETFL);
	if (fl < 0 || !(fl & O_NONBLOCK))
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

static int test_msg_peek(void)
{
	int sv[2];
	char buf[8];
	ssize_t n;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
		return -1;
	if (send(sv[0], "PK", 2, 0) != 2)
		goto fail;
	n = recv(sv[1], buf, sizeof(buf), MSG_PEEK);
	if (n != 2 || buf[0] != 'P' || buf[1] != 'K')
		goto fail;
	memset(buf, 0, sizeof(buf));
	n = recv(sv[1], buf, sizeof(buf), 0);
	if (n != 2 || buf[0] != 'P' || buf[1] != 'K')
		goto fail;
	close(sv[0]);
	close(sv[1]);
	return 0;
fail:
	close(sv[0]);
	close(sv[1]);
	return -1;
}

static int test_reuseaddr(void)
{
	int s1, s2;
	struct sockaddr_un addr;
	int on = 1;
	int got = 0;
	socklen_t glen = sizeof(got);
	const char name[] = "ir0ru";

	s1 = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s1 < 0)
		return -1;
	if (setsockopt(s1, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0)
		goto fail1;
	if (getsockopt(s1, SOL_SOCKET, SO_REUSEADDR, &got, &glen) != 0 || got != 1)
		goto fail1;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	addr.sun_path[0] = '\0';
	memcpy(addr.sun_path + 1, name, sizeof(name) - 1);
	if (bind(s1, (struct sockaddr *)&addr,
		 offsetof(struct sockaddr_un, sun_path) + 1 + sizeof(name) - 1) != 0)
		goto fail1;
	s2 = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s2 < 0)
		goto fail1;
	if (setsockopt(s2, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0)
		goto fail2;
	if (bind(s2, (struct sockaddr *)&addr,
		 offsetof(struct sockaddr_un, sun_path) + 1 + sizeof(name) - 1) != 0)
		goto fail2;
	close(s2);
	close(s1);
	return 0;
fail2:
	close(s2);
fail1:
	close(s1);
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
		say("KTM_UNIX_FLAGS_FAIL open\n");
		return 1;
	}
	if (ktm_get_caps(kfd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_UNIX_FLAGS_FAIL caps\n");
		ktm_close(kfd);
		return 1;
	}
	(void)ktm_reset(kfd);
	if (ktm_case_begin(kfd, "unix_flags") != 0)
	{
		say("KTM_UNIX_FLAGS_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}
	if (test_sock_flags() != 0)
	{
		(void)ktm_assert_true(kfd, "sock_flags", 0);
		fails++;
	}
	else
		(void)ktm_assert_true(kfd, "sock_flags", 1);
	if (test_accept4() != 0)
	{
		(void)ktm_assert_true(kfd, "accept4", 0);
		fails++;
	}
	else
	{
		(void)ktm_assert_true(kfd, "accept4", 1);
		say("ACCEPT4_OK\n");
	}
	if (test_msg_peek() != 0)
	{
		(void)ktm_assert_true(kfd, "msg_peek", 0);
		fails++;
	}
	else
	{
		(void)ktm_assert_true(kfd, "msg_peek", 1);
		say("MSG_PEEK_OK\n");
	}
	if (test_reuseaddr() != 0)
	{
		(void)ktm_assert_true(kfd, "reuseaddr", 0);
		fails++;
	}
	else
	{
		(void)ktm_assert_true(kfd, "reuseaddr", 1);
		say("SO_REUSEADDR_OK\n");
	}
	if (fails == 0)
	{
		say("UNIX_FLAGS_OK\n");
		say("KTM_UNIX_FLAGS_OK\n");
	}
	(void)ktm_case_end(kfd, "unix_flags", fails == 0 ? 0 : 1);
	ktm_close(kfd);
	say(fails == 0 ? "KTM_USERDEV_OK\n" : "KTM_USERDEV_FAIL\n");
	return fails == 0 ? 0 : 1;
}
