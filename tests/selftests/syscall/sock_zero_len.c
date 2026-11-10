/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: sock_zero_len.c
 * Description: send/sendto/recv with a zero length succeed and move no data
 *              (Linux: a stream reports 0 and keeps its queue, a datagram
 *              socket sends an empty datagram). A zero length needs no
 *              buffer: a NULL one is not an error.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

/* struct sockaddr_in; nolibc has no <netinet/in.h>. */
struct probe_sockaddr_in
{
	unsigned short family;
	unsigned short port_be;
	unsigned int addr_be;
	unsigned char zero[8];
};

/* 127.0.0.1:9 (discard), both in network byte order on little endian. */
#define PROBE_LOOPBACK_BE 0x0100007FU
#define PROBE_PORT9_BE 0x0900U

static int ok_ret(long ret, long want)
{
	if (ret == want)
		return 1;
	ksft_print_num("ret", ret);
	ksft_print_num("errno", errno);
	return 0;
}

int main(void)
{
	static const char byte = 'x';
	struct probe_sockaddr_in sin = { 0 };
	char buf[4] = { 0, 0, 0, 0 };
	int sv[2] = { -1, -1 };
	long ret;
	int udp;

	ksft_set_plan(8);

	ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
	ksft_test_result(ok_ret(ret, 0), "socketpair_stream");

	errno = 0;
	ret = send(sv[0], &byte, 0, 0);
	ksft_test_result(ok_ret(ret, 0), "stream_send_zero_len_returns_0");

	errno = 0;
	(void)send(sv[0], &byte, 1, 0);
	ret = recv(sv[1], buf, 0, MSG_DONTWAIT);
	ksft_test_result(ok_ret(ret, 0), "stream_recv_zero_len_returns_0");

	errno = 0;
	ret = recv(sv[1], buf, 1, MSG_DONTWAIT);
	ksft_test_result(ok_ret(ret, 1) && buf[0] == 'x',
			 "stream_recv_zero_len_keeps_queue");

	errno = 0;
	udp = socket(AF_INET, SOCK_DGRAM, 0);
	sin.family = AF_INET;
	sin.port_be = PROBE_PORT9_BE;
	sin.addr_be = PROBE_LOOPBACK_BE;
	ret = udp < 0 ? -1 : sendto(udp, &byte, 0, 0, (const void *)&sin,
				    sizeof(sin));
	ksft_test_result(ok_ret(ret, 0), "dgram_sendto_zero_len_returns_0");

	errno = 0;
	ret = send(sv[0], 0, 0, 0);
	ksft_test_result(ok_ret(ret, 0), "stream_send_null_zero_len_returns_0");

	errno = 0;
	(void)send(sv[0], &byte, 1, 0);
	ret = recv(sv[1], 0, 0, MSG_DONTWAIT);
	ksft_test_result(ok_ret(ret, 0) &&
			 recv(sv[1], buf, 1, MSG_DONTWAIT) == 1,
			 "stream_recv_null_zero_len_returns_0");

	errno = 0;
	ret = udp < 0 ? -1 : sendto(udp, 0, 0, 0, (const void *)&sin,
				    sizeof(sin));
	ksft_test_result(ok_ret(ret, 0), "dgram_sendto_null_zero_len_returns_0");
	return ksft_exit();
}
