/**
 * IR0 userspace — KTM NIC reach case (F8-1 slice)
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_nic_reach_case.c
 * Description: Probe /dev/net (open + ifconfig write). Optional ping to
 *              10.0.2.2. Pass on probe; ping reply is best-effort.
 *              Optional virtio-9p host share report.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libktm_user.h"

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

static int probe_dev_net(void)
{
	struct stat st;
	int fd;
	ssize_t n;
	const char *ifc = "ifconfig\n";
	const char *ping = "ping 10.0.2.2\n";

	if (stat("/dev/net", &st) != 0)
		return -1;
	fd = open("/dev/net", O_RDWR);
	if (fd < 0)
		return -1;

	n = write(fd, ifc, strlen(ifc));
	if (n < 0)
	{
		close(fd);
		return -1;
	}
	say("NIC_IFCONFIG_WRITE_OK\n");

	/* Best-effort ping; do not fail the case if echo reply is missing. */
	n = write(fd, ping, strlen(ping));
	if (n >= 0)
		say("NIC_PING_SUBMIT_OK\n");
	else
		say("NIC_PING_SUBMIT_SKIP\n");

	close(fd);
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
	payload = ok ? "F8_NIC_REACH_OK\n" : "F8_NIC_REACH_FAIL\n";
	fd = open("/mnt/host/ktm_nic_reach.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
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
	int ok;
	ktm_user_caps_t caps;

	kfd = ktm_open();
	if (kfd < 0)
	{
		say("F8_NIC_REACH_FAIL open\n");
		return 1;
	}
	if (ktm_get_caps(kfd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("F8_NIC_REACH_FAIL caps\n");
		ktm_close(kfd);
		return 1;
	}
	(void)ktm_reset(kfd);
	if (ktm_case_begin(kfd, "nic_reach") != 0)
	{
		say("F8_NIC_REACH_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}

	(void)ktm_checkpoint(kfd, "nic_probe");
	ok = (probe_dev_net() == 0);
	if (!ok)
		fails++;
	if (ktm_assert_true(kfd, "dev_net_probe", ok) != 0)
		fails++;

	(void)ktm_case_end(kfd, "nic_reach", fails == 0 ? 0 : 1);
	ktm_close(kfd);

	try_hostshare_report(fails == 0);

	if (fails == 0)
	{
		say("NIC_REACH_OK\n");
		say("F8_NIC_REACH_OK\n");
		for (;;)
			(void)pause();
		return 0;
	}
	say("F8_NIC_REACH_FAIL\n");
	for (;;)
		(void)pause();
	return 1;
}
