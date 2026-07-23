/**
 * IR0 userspace — KTM NIC reach case (F8-1 slice)
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_nic_reach_case.c
 * Description: Probe /dev/net (ifconfig + ICMP ping to 10.0.2.2). Pass only
 *              when ping_result success=1 is readable. Optional virtio-9p report.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
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
	char buf[512];
	int tries;
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

	n = write(fd, ping, strlen(ping));
	if (n < 0)
	{
		say("NIC_PING_SUBMIT_FAIL\n");
		close(fd);
		return -1;
	}
	say("NIC_PING_SUBMIT_OK\n");

	for (tries = 0; tries < 80; tries++)
	{
		n = read(fd, buf, sizeof(buf) - 1);
		if (n > 0)
		{
			buf[n] = '\0';
			if (strstr(buf, "type=ping_result") &&
			    strstr(buf, "success=1"))
			{
				say("NIC_PING_REPLY_OK\n");
				close(fd);
				return 0;
			}
		}
		usleep(50000);
	}

	say("NIC_PING_REPLY_FAIL\n");
	close(fd);
	return -1;
}

static void try_hostshare_report(int ok)
{
	const char *payload = ok ? "F8_NIC_REACH_OK\n" : "F8_NIC_REACH_FAIL\n";
	(void)ktm_hostshare_report("ktm_nic_reach.txt", payload);
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
	if (ktm_case_begin(kfd, "nic_reach") != 0)
	{
		say("F8_NIC_REACH_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}

	ok = (probe_dev_net() == 0);
	ktm_assert_true(kfd, "nic_reach_probe", ok);
	if (!ok)
		fails++;

	try_hostshare_report(ok);
	if (ok)
		say("F8_NIC_REACH_OK\n");
	else
		say("F8_NIC_REACH_FAIL\n");

	ktm_case_end(kfd, "nic_reach", fails == 0 ? 0 : 1);
	ktm_close(kfd);
	return fails == 0 ? 0 : 1;
}
