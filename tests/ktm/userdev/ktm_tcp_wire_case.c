/**
 * IR0 userspace — KTM wire TCP case (F8-3 slice)
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_tcp_wire_case.c
 * Description: connect+send+recv to QEMU gateway 10.0.2.2:8888 (host echo).
 *              Host listener must accept WIRETCP and echo WIREECHO.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "libktm_user.h"

#define WIRE_PORT 8888

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

static int test_tcp_wire(void)
{
	int fd;
	struct sockaddr_in addr;
	const char *msg = "WIRETCP\n";
	char rbuf[64];
	ssize_t n;
	int tries;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(WIRE_PORT);
	addr.sin_addr.s_addr = htonl((10U << 24) | (0U << 16) | (2U << 8) | 2U);
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
	{
		close(fd);
		return -1;
	}
	say("F8_TCP_WIRE_CONNECT_OK\n");

	n = send(fd, msg, strlen(msg), 0);
	if (n != (ssize_t)strlen(msg))
	{
		close(fd);
		return -1;
	}

	for (tries = 0; tries < 40; tries++)
	{
		n = recv(fd, rbuf, sizeof(rbuf), 0);
		if (n > 0)
			break;
		usleep(50000);
	}
	close(fd);
	if (n <= 0 || memcmp(rbuf, "WIREECHO", 8) != 0)
		return -1;

	say("F8_TCP_WIRE_SENDRECV_OK\n");
	return 0;
}

static void try_hostshare_report(int ok)
{
	const char *payload = ok ? "F8_TCP_WIRE_OK\n" : "F8_TCP_WIRE_FAIL\n";
	(void)ktm_hostshare_report("ktm_tcp_wire.txt", payload);
}


int main(void)
{
	int kfd;
	int fails = 0;
	int wire_ok;
	ktm_user_caps_t caps;

	kfd = ktm_open();
	if (kfd < 0)
	{
		say("F8_TCP_WIRE_FAIL open\n");
		return 1;
	}
	if (ktm_get_caps(kfd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("F8_TCP_WIRE_FAIL caps\n");
		ktm_close(kfd);
		return 1;
	}
	if (ktm_case_begin(kfd, "tcp_wire") != 0)
	{
		say("F8_TCP_WIRE_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}

	wire_ok = (test_tcp_wire() == 0);
	ktm_assert_true(kfd, "tcp_wire", wire_ok);
	if (!wire_ok)
		fails++;

	try_hostshare_report(wire_ok);
	if (wire_ok)
		say("F8_TCP_WIRE_OK\n");
	else
		say("F8_TCP_WIRE_FAIL\n");

	ktm_case_end(kfd, "tcp_wire", fails == 0 ? 0 : 1);
	ktm_close(kfd);
	return fails == 0 ? 0 : 1;
}
