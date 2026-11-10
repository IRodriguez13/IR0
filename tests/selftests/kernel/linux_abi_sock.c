/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: linux_abi_sock.c
 * Description: RED selftest (fails on dev 629036ef, not wired into any gate):
 *              socket syscalls that diverge from Linux, found by the
 *              conformance harness (Documentation/conformance.md). One case
 *              per bug; each passes on Linux. Run by
 *              make ir0-selftest-red-sock-{x86,arm64}.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "conf.h"

#include <sys/socket.h>
#include <sys/un.h>

static unsigned int unix_addr(struct sockaddr_un *a, const char *path)
{
	rt_fill(a, 0, sizeof(*a));
	a->sun_family = AF_UNIX;
	rt_copy(a->sun_path, path, rt_strlen(path));
	return (unsigned int)(sizeof(a->sun_family) + rt_strlen(path) + 1);
}

/* net/socket.c __sock_create(): an unknown family is EAFNOSUPPORT. */
int sock_bad_family_eafnosupport(void)
{
	EXPECT_ERR(S3(SYS_socket, 9999, SOCK_STREAM, 0), EAFNOSUPPORT);
	return CONF_PASS;
}

/* sendto(2) on a file descriptor that is not a socket is ENOTSOCK. */
int sock_send_non_socket_enotsock(void)
{
	int p[2];

	EXPECT(S2(SYS_pipe2, p, 0), 0);
	EXPECT_ERR(S6(SYS_sendto, p[1], "x", 1, 0, 0, 0), ENOTSOCK);
	return CONF_PASS;
}

/* net/unix/af_unix.c unix_bind(): binding a bound socket again is EINVAL. */
int sock_bind_twice_einval(void)
{
	struct sockaddr_un a;
	unsigned int len;
	long s = S3(SYS_socket, AF_UNIX, SOCK_STREAM, 0);

	CHECK(s >= 0);
	len = unix_addr(&a, "one");
	EXPECT(S3(SYS_bind, s, &a, len), 0);
	len = unix_addr(&a, "two");
	EXPECT_ERR(S3(SYS_bind, s, &a, len), EINVAL);
	return CONF_PASS;
}

/* connect(2) to a socket path that does not exist is ENOENT. */
int sock_connect_missing_path_enoent(void)
{
	struct sockaddr_un a;
	unsigned int len = unix_addr(&a, "missing");
	long s = S3(SYS_socket, AF_UNIX, SOCK_STREAM, 0);

	CHECK(s >= 0);
	EXPECT_ERR(S3(SYS_connect, s, &a, len), ENOENT);
	return CONF_PASS;
}

/* getsockname(2) returns the bound path. */
int sock_getsockname_returns_path(void)
{
	struct sockaddr_un a;
	struct sockaddr_un got;
	unsigned int len = unix_addr(&a, "srv");
	unsigned int glen = sizeof(got);
	long s = S3(SYS_socket, AF_UNIX, SOCK_STREAM, 0);

	CHECK(s >= 0);
	EXPECT(S3(SYS_bind, s, &a, len), 0);
	rt_fill(&got, 0, sizeof(got));
	EXPECT(S3(SYS_getsockname, s, &got, &glen), 0);
	CHECK(rt_streq(got.sun_path, "srv"));
	return CONF_PASS;
}

/* unix(7): AF_UNIX supports SOCK_DGRAM (socketpair and bind). */
int sock_unix_dgram_supported(void)
{
	struct sockaddr_un a;
	unsigned int len = unix_addr(&a, "dg");
	int sv[2];
	long s;

	EXPECT(S4(SYS_socketpair, AF_UNIX, SOCK_DGRAM, 0, sv), 0);
	s = S3(SYS_socket, AF_UNIX, SOCK_DGRAM, 0);
	CHECK(s >= 0);
	EXPECT(S3(SYS_bind, s, &a, len), 0);
	return CONF_PASS;
}
