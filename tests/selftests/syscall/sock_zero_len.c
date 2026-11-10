/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: sock_zero_len.c
 * Description: send/sendto/recv with a zero length succeed and move no data
 *              (Linux: a stream reports 0 and keeps its queue, a datagram
 *              socket sends an empty datagram). A zero length needs no
 *              buffer: a NULL one is not an error. Datagrams to
 *              127.0.0.1 reach the local socket bound to that port.
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
/* errno(3) EAGAIN; nolibc has no <errno.h>. */
#define LINUX_EAGAIN 11
/* 127.0.0.1:7777, the port of the receiving datagram socket. */
#define PROBE_PORT7777_BE 0x611EU

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
	struct probe_sockaddr_in rx_sin = { 0 };
	char buf[4] = { 0, 0, 0, 0 };
	int sv[2] = { -1, -1 };
	long ret;
	int udp;
	int udp_rx;

	ksft_set_plan(11);

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

	errno = 0;
	udp_rx = socket(AF_INET, SOCK_DGRAM, 0);
	rx_sin.family = AF_INET;
	rx_sin.port_be = PROBE_PORT7777_BE;
	rx_sin.addr_be = PROBE_LOOPBACK_BE;
	ret = udp_rx < 0 ? -1 : bind(udp_rx, (const void *)&rx_sin,
				     sizeof(rx_sin));
	if (ret == 0)
		ret = sendto(udp, "ab", 2, 0, (const void *)&rx_sin,
			     sizeof(rx_sin));
	if (ret == 2)
		ret = recv(udp_rx, buf, sizeof(buf), MSG_DONTWAIT);
	ksft_test_result(ok_ret(ret, 2) && buf[0] == 'a' && buf[1] == 'b',
			 "dgram_loopback_roundtrip");

	/* An empty datagram is a datagram: it is sent and received as 0. */
	errno = 0;
	ret = sendto(udp, &byte, 0, 0, (const void *)&rx_sin, sizeof(rx_sin));
	if (ret == 0)
		ret = recv(udp_rx, buf, sizeof(buf), MSG_DONTWAIT);
	ksft_test_result(ok_ret(ret, 0), "dgram_empty_datagram_received");

	/* A zero-length read still consumes the datagram (truncated). */
	errno = 0;
	ret = sendto(udp, "ab", 2, 0, (const void *)&rx_sin, sizeof(rx_sin));
	if (ret == 2)
		ret = recv(udp_rx, buf, 0, MSG_DONTWAIT);
	if (ret == 0)
	{
		ret = recv(udp_rx, buf, sizeof(buf), MSG_DONTWAIT);
		if (ret == -1 && errno == LINUX_EAGAIN)
			ret = 0;
	}
	ksft_test_result(ok_ret(ret, 0), "dgram_recv_zero_len_consumes");
	return ksft_exit();
}
