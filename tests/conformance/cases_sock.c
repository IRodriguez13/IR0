/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: cases_sock.c
 * Description: Conformance cases for AF_UNIX sockets. Expected results
 *              follow Linux (net/socket.c, net/unix/af_unix.c) and unix(7).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "conf.h"

#include <sys/socket.h>
#include <sys/un.h>

#define WAIT_MS 5000L

static unsigned int unix_addr(struct sockaddr_un *a, const char *path)
{
	rt_fill(a, 0, sizeof(*a));
	a->sun_family = AF_UNIX;
	rt_copy(a->sun_path, path, rt_strlen(path));
	return (unsigned int)(sizeof(a->sun_family) + rt_strlen(path) + 1);
}

int sock_pair_stream(void)
{
	char buf[16];
	int sv[2];
	long r;

	OK(r, S4(SYS_socketpair, AF_UNIX, SOCK_STREAM, 0, sv));
	EXPECT(S3(SYS_write, sv[0], "ping", 4), 4);
	EXPECT(S3(SYS_read, sv[1], buf, sizeof(buf)), 4);
	CHECK(rt_memeq(buf, "ping", 4));
	EXPECT(S6(SYS_sendto, sv[1], "pong", 4, 0, 0, 0), 4);
	EXPECT(S6(SYS_recvfrom, sv[0], buf, sizeof(buf), 0, 0, 0), 4);
	CHECK(rt_memeq(buf, "pong", 4));
	return CONF_PASS;
}

int sock_pair_dgram_boundaries(void)
{
	char buf[16];
	int sv[2];
	long r;

	OK(r, S4(SYS_socketpair, AF_UNIX, SOCK_DGRAM, 0, sv));
	EXPECT(S3(SYS_write, sv[0], "abc", 3), 3);
	EXPECT(S3(SYS_write, sv[0], "defgh", 5), 5);
	EXPECT(S3(SYS_read, sv[1], buf, sizeof(buf)), 3);
	EXPECT(S3(SYS_read, sv[1], buf, sizeof(buf)), 5);
	CHECK(rt_memeq(buf, "defgh", 5));
	/* A short buffer truncates the datagram; the rest is discarded. */
	EXPECT(S3(SYS_write, sv[0], "123456", 6), 6);
	EXPECT(S3(SYS_write, sv[0], "z", 1), 1);
	EXPECT(S3(SYS_read, sv[1], buf, 2), 2);
	EXPECT(S3(SYS_read, sv[1], buf, sizeof(buf)), 1);
	CHECK(buf[0] == 'z');
	return CONF_PASS;
}

int sock_nonblock_eagain(void)
{
	char buf[4];
	int sv[2];
	long r;

	OK(r, S4(SYS_socketpair, AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv));
	EXPECT_ERR(S3(SYS_read, sv[0], buf, sizeof(buf)), EAGAIN);
	OK(r, S4(SYS_socketpair, AF_UNIX, SOCK_STREAM, 0, sv));
	EXPECT_ERR(S6(SYS_recvfrom, sv[0], buf, sizeof(buf), MSG_DONTWAIT, 0, 0),
		   EAGAIN);
	return CONF_PASS;
}

int sock_shutdown_eof(void)
{
	char buf[4];
	int sv[2];
	long r;

	OK(r, S4(SYS_socketpair, AF_UNIX, SOCK_STREAM, 0, sv));
	EXPECT(S3(SYS_write, sv[0], "x", 1), 1);
	EXPECT(S2(SYS_shutdown, sv[0], SHUT_WR), 0);
	EXPECT(S3(SYS_read, sv[1], buf, sizeof(buf)), 1);
	EXPECT(S3(SYS_read, sv[1], buf, sizeof(buf)), 0);
	EXPECT_ERR(S6(SYS_sendto, sv[0], "y", 1, MSG_NOSIGNAL, 0, 0), EPIPE);
	EXPECT_ERR(S2(SYS_shutdown, sv[0], 7), EINVAL);
	return CONF_PASS;
}

int sock_peer_closed_epipe(void)
{
	char buf[4];
	int sv[2];
	long r;

	OK(r, S4(SYS_socketpair, AF_UNIX, SOCK_STREAM, 0, sv));
	EXPECT(S1(SYS_close, sv[1]), 0);
	EXPECT_ERR(S6(SYS_sendto, sv[0], "y", 1, MSG_NOSIGNAL, 0, 0), EPIPE);
	EXPECT(S3(SYS_read, sv[0], buf, sizeof(buf)), 0);
	return CONF_PASS;
}

int sock_create_errors(void)
{
	int sv[2];

	EXPECT_ERR(S3(SYS_socket, 9999, SOCK_STREAM, 0), EAFNOSUPPORT);
	EXPECT_ERR(S3(SYS_socket, AF_UNIX, 9999, 0), EINVAL);
	EXPECT_ERR(S3(SYS_socket, AF_UNIX, SOCK_STREAM, 99), EPROTONOSUPPORT);
	EXPECT_ERR(S4(SYS_socketpair, AF_UNIX, SOCK_STREAM, 0, 0), EFAULT);
	EXPECT_ERR(S4(SYS_socketpair, 9999, SOCK_STREAM, 0, sv), EAFNOSUPPORT);
	return CONF_PASS;
}

int sock_not_a_socket(void)
{
	char buf[4];
	int p[2];
	long r;

	OK(r, S2(SYS_pipe2, p, 0));
	EXPECT_ERR(S6(SYS_sendto, p[1], "x", 1, 0, 0, 0), ENOTSOCK);
	EXPECT_ERR(S6(SYS_recvfrom, p[0], buf, 1, MSG_DONTWAIT, 0, 0), ENOTSOCK);
	EXPECT_ERR(S2(SYS_listen, p[0], 1), ENOTSOCK);
	EXPECT_ERR(S6(SYS_sendto, -1, "x", 1, 0, 0, 0), EBADF);
	return CONF_PASS;
}

int sock_bind_listen_accept(void)
{
	struct sockaddr_un addr;
	unsigned int len = unix_addr(&addr, "srv");
	char buf[8];
	int status = 0;
	long s;
	long c;
	long pid;

	OK(s, S3(SYS_socket, AF_UNIX, SOCK_STREAM, 0));
	EXPECT(S3(SYS_bind, s, &addr, len), 0);
	EXPECT(S2(SYS_listen, s, 4), 0);
	OK(pid, rt_fork());
	if (pid == 0)
	{
		long fd = S3(SYS_socket, AF_UNIX, SOCK_STREAM, 0);

		if (fd < 0 || S3(SYS_connect, fd, &addr, len) != 0)
			rt_exit(1);
		rt_exit(S3(SYS_write, fd, "hi!", 3) == 3 ? 0 : 2);
	}
	OK(c, S4(SYS_accept4, s, 0, 0, 0));
	EXPECT(S3(SYS_read, c, buf, 3), 3);
	CHECK(rt_memeq(buf, "hi!", 3));
	EXPECT(conf_child_status(pid, &status, WAIT_MS), pid);
	EXPECT(status, 0);
	return CONF_PASS;
}

int sock_bind_errors(void)
{
	struct sockaddr_un addr;
	unsigned int len = unix_addr(&addr, "srv");
	long s;
	long t;

	OK(s, S3(SYS_socket, AF_UNIX, SOCK_STREAM, 0));
	OK(t, S3(SYS_socket, AF_UNIX, SOCK_STREAM, 0));
	EXPECT(S3(SYS_bind, s, &addr, len), 0);
	EXPECT_ERR(S3(SYS_bind, t, &addr, len), EADDRINUSE);
	len = unix_addr(&addr, "other");
	EXPECT_ERR(S3(SYS_bind, s, &addr, len), EINVAL);
	len = unix_addr(&addr, "missing/srv");
	EXPECT_ERR(S3(SYS_bind, t, &addr, len), ENOENT);
	EXPECT_ERR(S3(SYS_bind, t, 0, len), EFAULT);
	return CONF_PASS;
}

int sock_connect_errors(void)
{
	struct sockaddr_un addr;
	unsigned int len = unix_addr(&addr, "srv");
	long s;
	long c;

	OK(c, S3(SYS_socket, AF_UNIX, SOCK_STREAM, 0));
	EXPECT_ERR(S3(SYS_connect, c, &addr, len), ENOENT);
	OK(s, S3(SYS_socket, AF_UNIX, SOCK_STREAM, 0));
	EXPECT(S3(SYS_bind, s, &addr, len), 0);
	/* Bound but not listening. */
	EXPECT_ERR(S3(SYS_connect, c, &addr, len), ECONNREFUSED);
	return CONF_PASS;
}

int sock_getsockname(void)
{
	struct sockaddr_un addr;
	struct sockaddr_un got;
	unsigned int len = unix_addr(&addr, "srv");
	unsigned int glen = sizeof(got);
	long s;

	OK(s, S3(SYS_socket, AF_UNIX, SOCK_STREAM, 0));
	EXPECT(S3(SYS_bind, s, &addr, len), 0);
	rt_fill(&got, 0, sizeof(got));
	EXPECT(S3(SYS_getsockname, s, &got, &glen), 0);
	EXPECT(got.sun_family, AF_UNIX);
	CHECK(rt_streq(got.sun_path, "srv"));
	EXPECT(glen, len);
	return CONF_PASS;
}

int sock_dgram_bound_sendto(void)
{
	struct sockaddr_un addr;
	unsigned int len = unix_addr(&addr, "dg");
	char buf[8];
	long s;
	long c;

	OK(s, S3(SYS_socket, AF_UNIX, SOCK_DGRAM, 0));
	EXPECT(S3(SYS_bind, s, &addr, len), 0);
	OK(c, S3(SYS_socket, AF_UNIX, SOCK_DGRAM, 0));
	EXPECT(S6(SYS_sendto, c, "dgram", 5, 0, &addr, len), 5);
	EXPECT(S6(SYS_recvfrom, s, buf, sizeof(buf), 0, 0, 0), 5);
	CHECK(rt_memeq(buf, "dgram", 5));
	EXPECT_ERR(S6(SYS_sendto, c, "x", 1, 0, 0, 0), ENOTCONN);
	return CONF_PASS;
}
