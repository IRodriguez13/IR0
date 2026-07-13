/**
 * IR0 userspace — KTM wire TCP case (F8-3 slice)
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_tcp_wire_case.c
 * Description: connect+send to QEMU gateway 10.0.2.2:8888 (host listener).
 *              Host listener must accept WIRETCP. Optional virtio-9p report.
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
#include <sys/stat.h>
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
	ssize_t n;

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
	close(fd);
	if (n != (ssize_t)strlen(msg))
		return -1;

	say("F8_TCP_WIRE_SENDRECV_OK\n");
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
	payload = ok ? "F8_TCP_WIRE_OK\n" : "F8_TCP_WIRE_FAIL\n";
	fd = open("/mnt/host/ktm_tcp_wire.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
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
	(void)ktm_reset(kfd);
	if (ktm_case_begin(kfd, "tcp_wire") != 0)
	{
		say("F8_TCP_WIRE_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}

	(void)ktm_checkpoint(kfd, "tcp_wire_connect");
	wire_ok = (test_tcp_wire() == 0);
	if (!wire_ok)
		fails++;
	if (ktm_assert_true(kfd, "tcp_wire_send", wire_ok) != 0)
		fails++;

	(void)ktm_case_end(kfd, "tcp_wire", fails == 0 ? 0 : 1);
	ktm_close(kfd);

	try_hostshare_report(fails == 0);

	if (fails == 0)
	{
		say("F8_TCP_WIRE_OK\n");
		_exit(0);
	}
	say("F8_TCP_WIRE_FAIL\n");
	_exit(1);
}
