/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: accept4.c
 * Description: accept4 writes the peer's AF_UNIX name into the user buffer.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#define A_SOCKET	198
#define A_BIND		200
#define A_LISTEN	201
#define A_CONNECT	203
#define A_ACCEPT4	242
#else
#include <sys/syscall.h>
#include <unistd.h>
#define A_SOCKET	SYS_socket
#define A_BIND		SYS_bind
#define A_LISTEN	SYS_listen
#define A_CONNECT	SYS_connect
#define A_ACCEPT4	SYS_accept4
#endif

#include "ir0/ksft.h"

struct aaddr
{
	unsigned short family;
	unsigned char path[16];
};

static void fill_abstract(struct aaddr *a, char tag)
{
	unsigned int i;

	a->family = 1;
	for (i = 0; i < sizeof(a->path); i++)
		a->path[i] = 0;
	a->path[0] = 0;
	a->path[1] = 'a';
	a->path[2] = 'c';
	a->path[3] = 'c';
	a->path[4] = (unsigned char)tag;
}

static int call4(long nr, long a, long b, long c, long d)
{
	return (int)syscall(nr, a, b, c, d);
}

int main(void)
{
	struct aaddr lst;
	struct aaddr cli;
	struct aaddr got;
	unsigned int glen = sizeof(got);
	unsigned int i;
	int lfd;
	int cfd;
	int afd;
	int ok;
	const unsigned int namelen = 2u + 5u;

	ksft_set_plan(1);
	fill_abstract(&lst, 'L');
	fill_abstract(&cli, 'C');
	for (i = 0; i < sizeof(got); i++)
		((unsigned char *)&got)[i] = 0;

	lfd = call4(A_SOCKET, 1, 1, 0, 0);
	cfd = call4(A_SOCKET, 1, 1, 0, 0);
	if (lfd < 0 || cfd < 0)
	{
		ksft_print_num("socket", lfd < 0 ? lfd : cfd);
		ksft_test_result(0, "accept4_peer_addr");
		return ksft_exit();
	}
	if (call4(A_BIND, lfd, (long)&lst, namelen, 0) < 0 ||
	    call4(A_BIND, cfd, (long)&cli, namelen, 0) < 0 ||
	    call4(A_LISTEN, lfd, 1, 0, 0) < 0 ||
	    call4(A_CONNECT, cfd, (long)&lst, namelen, 0) < 0)
	{
		ksft_print_num("setup", -1);
		ksft_test_result(0, "accept4_peer_addr");
		return ksft_exit();
	}
	afd = call4(A_ACCEPT4, lfd, (long)&got, (long)&glen, 0);
	ok = afd >= 0 && got.family == 1 && glen == namelen &&
	     got.path[0] == 0 && got.path[1] == 'a' && got.path[2] == 'c' &&
	     got.path[3] == 'c' && got.path[4] == 'C';
	if (!ok)
	{
		ksft_print_num("afd", afd);
		ksft_print_num("family", got.family);
		ksft_print_num("glen", glen);
		ksft_print_num("tag", got.path[4]);
	}
	ksft_test_result(ok, "accept4_peer_addr");
	return ksft_exit();
}
