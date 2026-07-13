/**
 * IR0 userspace — KTM TCP guest-net case (F8-2 slice)
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_tcp_guest_case.c
 * Description: AF_INET stream connect on guest IP 10.0.2.15 (beyond loopback).
 *              Optional virtio-9p host share report.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <unistd.h>

#include "libktm_user.h"

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

static int test_tcp_guest(void)
{
	int srv;
	int cli;
	int acc;
	struct sockaddr_in addr;
	char buf[32];
	ssize_t n;
	const char *msg = "GUESTTCP";

	srv = socket(AF_INET, SOCK_STREAM, 0);
	if (srv < 0)
		return -1;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(9876);
	addr.sin_addr.s_addr = htonl((10U << 24) | (0U << 16) | (2U << 8) | 15U);
	if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0)
	{
		close(srv);
		return -1;
	}
	if (listen(srv, 1) != 0)
	{
		close(srv);
		return -1;
	}

	cli = socket(AF_INET, SOCK_STREAM, 0);
	if (cli < 0)
	{
		close(srv);
		return -1;
	}
	if (connect(cli, (struct sockaddr *)&addr, sizeof(addr)) != 0)
	{
		close(cli);
		close(srv);
		return -1;
	}

	acc = accept(srv, NULL, NULL);
	if (acc < 0)
	{
		close(cli);
		close(srv);
		return -1;
	}
	if (send(cli, msg, 8, 0) != 8)
	{
		close(acc);
		close(cli);
		close(srv);
		return -1;
	}
	n = recv(acc, buf, sizeof(buf), 0);
	if (n != 8 || memcmp(buf, msg, 8) != 0)
	{
		close(acc);
		close(cli);
		close(srv);
		return -1;
	}

	close(acc);
	close(cli);
	close(srv);
	say("F8_TCP_GUEST_SENDRECV_OK\n");
	return 0;
}

static void try_hostshare_report(int ok)
{
	const char *payload;
	int fd;
	ssize_t n;

	(void)mkdir("/mnt", 0755);
	(void)mkdir("/mnt/host", 0755);
	if (mount("ir0share", "/mnt/host", "9p", 0, NULL) != 0)
	{
		say("KTM_HOSTSHARE_SKIP\n");
		return;
	}
	say("KTM_HOSTSHARE_MOUNT_OK\n");
	payload = ok ? "F8_TCP_GUEST_OK\n" : "F8_TCP_GUEST_FAIL\n";
	fd = open("/mnt/host/ktm_tcp_guest.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
	{
		say("KTM_HOSTSHARE_WRITE_SKIP\n");
		return;
	}
	n = write(fd, payload, strlen(payload));
	(void)close(fd);
	if (n == (ssize_t)strlen(payload))
		say("KTM_HOSTSHARE_REPORT_OK\n");
	else
		say("KTM_HOSTSHARE_WRITE_SKIP\n");
}

int main(void)
{
	int kfd;
	int fails = 0;
	int tcp_ok;
	ktm_user_caps_t caps;

	kfd = ktm_open();
	if (kfd < 0)
	{
		say("F8_TCP_GUEST_FAIL open\n");
		return 1;
	}
	if (ktm_get_caps(kfd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("F8_TCP_GUEST_FAIL caps\n");
		ktm_close(kfd);
		return 1;
	}
	(void)ktm_reset(kfd);
	if (ktm_case_begin(kfd, "tcp_guest") != 0)
	{
		say("F8_TCP_GUEST_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}

	(void)ktm_checkpoint(kfd, "tcp_guest_bind");
	tcp_ok = (test_tcp_guest() == 0);
	if (!tcp_ok)
		fails++;
	if (ktm_assert_true(kfd, "tcp_guest_sendrecv", tcp_ok) != 0)
		fails++;

	(void)ktm_case_end(kfd, "tcp_guest", fails == 0 ? 0 : 1);
	ktm_close(kfd);

	try_hostshare_report(fails == 0);

	if (fails == 0)
	{
		say("F8_TCP_GUEST_OK\n");
		for (;;)
			(void)pause();
		return 0;
	}
	say("F8_TCP_GUEST_FAIL\n");
	for (;;)
		(void)pause();
	return 1;
}
