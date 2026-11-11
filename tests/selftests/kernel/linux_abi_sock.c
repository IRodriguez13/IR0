/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: linux_abi_sock.c
 * Description: Selftest: socket syscalls that diverged from Linux, found by
 *              the conformance harness (Documentation/conformance.md). One
 *              case per fixed bug; each passes on Linux and on IR0. Run by
 *              make ir0-selftest-sock-abi-{x86,arm64}.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "conf.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>

static unsigned int unix_addr(struct sockaddr_un *a, const char *path)
{
	rt_fill(a, 0, sizeof(*a));
	a->sun_family = AF_UNIX;
	rt_copy(a->sun_path, path, rt_strlen(path));
	return (unsigned int)(sizeof(a->sun_family) + rt_strlen(path) + 1);
}

/* 127.0.0.1:@port, written byte by byte (no libc htons on arm64). */
static void inet_lo(struct sockaddr_in *a, unsigned int port)
{
	unsigned char *ip = (unsigned char *)&a->sin_addr;
	unsigned char *pp = (unsigned char *)&a->sin_port;

	rt_fill(a, 0, sizeof(*a));
	a->sin_family = AF_INET;
	pp[0] = (unsigned char)(port >> 8);
	pp[1] = (unsigned char)port;
	ip[0] = 127;
	ip[3] = 1;
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

/* unix(7): datagrams keep their boundaries and name their sender. */
int sock_unix_dgram_send_recv(void)
{
	struct sockaddr_un a;
	struct sockaddr_un from;
	unsigned int len = unix_addr(&a, "srv");
	unsigned int flen = sizeof(from);
	char buf[8];
	long s = S3(SYS_socket, AF_UNIX, SOCK_DGRAM, 0);
	long c = S3(SYS_socket, AF_UNIX, SOCK_DGRAM, 0);

	CHECK(s >= 0 && c >= 0);
	EXPECT(S3(SYS_bind, s, &a, len), 0);
	EXPECT_ERR(S6(SYS_sendto, c, "x", 1, 0, 0, 0), ENOTCONN);
	EXPECT(S6(SYS_sendto, c, "abc", 3, 0, &a, len), 3);
	EXPECT(S6(SYS_sendto, c, "defgh", 5, 0, &a, len), 5);
	EXPECT(S6(SYS_recvfrom, s, buf, sizeof(buf), 0, &from, &flen), 3);
	/* An unbound sender has no name. */
	EXPECT(flen, 0);
	EXPECT(S3(SYS_read, s, buf, 2), 2);
	CHECK(rt_memeq(buf, "de", 2));
	EXPECT(S3(SYS_connect, c, &a, len), 0);
	EXPECT(S3(SYS_write, c, "", 0), 0);
	EXPECT(S6(SYS_recvfrom, s, buf, sizeof(buf), MSG_DONTWAIT, 0, 0), 0);
	EXPECT_ERR(S6(SYS_recvfrom, s, buf, sizeof(buf), MSG_DONTWAIT, 0, 0),
		   EAGAIN);
	return CONF_PASS;
}

/* unix_dgram_sendmsg()/recvmsg(): gather, scatter and MSG_TRUNC. */
int sock_unix_dgram_msg(void)
{
	char a[3];
	char b[8];
	char out0[] = "abc";
	char out1[] = "defg";
	struct iovec out[2] = { { out0, 3 }, { out1, 4 } };
	struct iovec in[2] = { { a, sizeof(a) }, { b, 2 } };
	struct msghdr m;
	int sv[2];

	EXPECT(S4(SYS_socketpair, AF_UNIX, SOCK_DGRAM, 0, sv), 0);
	rt_fill(&m, 0, sizeof(m));
	m.msg_iov = out;
	m.msg_iovlen = 2;
	EXPECT(S3(SYS_sendmsg, sv[0], &m, 0), 7);
	rt_fill(&m, 0, sizeof(m));
	m.msg_iov = in;
	m.msg_iovlen = 2;
	EXPECT(S3(SYS_recvmsg, sv[1], &m, 0), 5);
	CHECK(rt_memeq(a, "abc", 3) && rt_memeq(b, "de", 2));
	CHECK(m.msg_flags & MSG_TRUNC);
	EXPECT_ERR(S3(SYS_recvmsg, sv[1], &m, MSG_DONTWAIT), EAGAIN);
	return CONF_PASS;
}

/* net/socket.c move_addr_to_kernel(): a NULL address is EFAULT. */
int sock_bind_null_addr_efault(void)
{
	long s = S3(SYS_socket, AF_UNIX, SOCK_STREAM, 0);

	CHECK(s >= 0);
	EXPECT_ERR(S3(SYS_bind, s, 0, sizeof(struct sockaddr_un)), EFAULT);
	return CONF_PASS;
}

/* unix_bind_bsd(): the parent directory must exist (ENOENT). */
int sock_bind_missing_dir_enoent(void)
{
	struct sockaddr_un a;
	unsigned int len = unix_addr(&a, "missing/srv");
	long s = S3(SYS_socket, AF_UNIX, SOCK_STREAM, 0);

	CHECK(s >= 0);
	EXPECT_ERR(S3(SYS_bind, s, &a, len), ENOENT);
	return CONF_PASS;
}

/*
 * udp_sendmsg(): send(2) without an address goes to the connected peer,
 * and is EDESTADDRREQ on an unconnected socket.
 */
int sock_udp_send_uses_peer(void)
{
	struct sockaddr_in a;
	char buf[8];
	long r = S3(SYS_socket, AF_INET, SOCK_DGRAM, 0);
	long c = S3(SYS_socket, AF_INET, SOCK_DGRAM, 0);

	CHECK(r >= 0 && c >= 0);
	inet_lo(&a, 40123);
	EXPECT(S3(SYS_bind, r, &a, sizeof(a)), 0);
	EXPECT_ERR(S6(SYS_sendto, c, "x", 1, 0, 0, 0), EDESTADDRREQ);
	EXPECT(S3(SYS_connect, c, &a, sizeof(a)), 0);
	EXPECT(S6(SYS_sendto, c, "peer", 4, 0, 0, 0), 4);
	EXPECT(S6(SYS_recvfrom, r, buf, sizeof(buf), MSG_DONTWAIT, 0, 0), 4);
	CHECK(rt_memeq(buf, "peer", 4));
	return CONF_PASS;
}

/* udp_sendmsg(): an explicit address wins over the connected peer. */
int sock_udp_sendto_overrides_peer(void)
{
	struct sockaddr_in a;
	struct sockaddr_in b;
	char buf[8];
	long ra = S3(SYS_socket, AF_INET, SOCK_DGRAM, 0);
	long rb = S3(SYS_socket, AF_INET, SOCK_DGRAM, 0);
	long c = S3(SYS_socket, AF_INET, SOCK_DGRAM, 0);

	CHECK(ra >= 0 && rb >= 0 && c >= 0);
	inet_lo(&a, 40125);
	inet_lo(&b, 40126);
	EXPECT(S3(SYS_bind, ra, &a, sizeof(a)), 0);
	EXPECT(S3(SYS_bind, rb, &b, sizeof(b)), 0);
	EXPECT(S3(SYS_connect, c, &a, sizeof(a)), 0);
	EXPECT(S6(SYS_sendto, c, "b", 1, 0, &b, sizeof(b)), 1);
	EXPECT(S6(SYS_recvfrom, rb, buf, sizeof(buf), MSG_DONTWAIT, 0, 0), 1);
	EXPECT_ERR(S6(SYS_recvfrom, ra, buf, sizeof(buf), MSG_DONTWAIT, 0, 0),
		   EAGAIN);
	return CONF_PASS;
}
