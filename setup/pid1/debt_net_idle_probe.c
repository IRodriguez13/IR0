/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: debt_net_idle_probe.c
 * Description: Guest probe for idle task, /sys/class/net/<iface>/*, SIOCGIFCONF.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>

static void emit(const char *s)
{
	size_t n = 0;

	if (!s)
		return;
	while (s[n])
		n++;
	(void)write(1, s, n);
}

static int read_file(const char *path, char *buf, size_t n)
{
	int fd;
	ssize_t r;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	r = read(fd, buf, n - 1);
	close(fd);
	if (r < 0)
		return -1;
	buf[r] = '\0';
	return 0;
}

int main(void)
{
	char buf[512];
	char path[64];
	int sock;
	struct ifconf ifc;
	char ifcbuf[1024];
	int ok = 1;
	const char *iface = "eth0";

	emit("DEBT_PROBE_START\n");

	if (read_file("/proc/uptime", buf, sizeof(buf)) != 0)
	{
		emit("DEBT_PROBE_FAIL uptime open\n");
		ok = 0;
	}
	else if (strchr(buf, ' ') == NULL)
	{
		emit("DEBT_PROBE_FAIL uptime format\n");
		ok = 0;
	}
	else
	{
		emit("DEBT_UPTIME_OK ");
		emit(buf);
		if (buf[0] && buf[strlen(buf) - 1] != '\n')
			emit("\n");
	}

	if (read_file("/sys/class/net/eth0/address", buf, sizeof(buf)) != 0)
	{
		iface = "virt0";
		if (read_file("/sys/class/net/virt0/address", buf, sizeof(buf)) != 0)
		{
			emit("DEBT_PROBE_FAIL no nic sysfs\n");
			ok = 0;
			goto out;
		}
	}
	emit("DEBT_SYSFS_ADDR_OK ");
	emit(iface);
	emit(" ");
	emit(buf);
	if (buf[0] && buf[strlen(buf) - 1] != '\n')
		emit("\n");

	memcpy(path, "/sys/class/net/", 15);
	path[15] = '\0';
	/* path = /sys/class/net/<iface>/rx_bytes */
	{
		size_t i = 0;
		size_t j;

		while (iface[i] && i + 15 < sizeof(path) - 16)
		{
			path[15 + i] = iface[i];
			i++;
		}
		j = 15 + i;
		memcpy(path + j, "/rx_bytes", 10);
	}
	if (read_file(path, buf, sizeof(buf)) != 0)
	{
		emit("DEBT_PROBE_FAIL rx_bytes\n");
		ok = 0;
	}
	else if (buf[0] == 'u' && buf[1] == 'n')
	{
		emit("DEBT_PROBE_FAIL rx_bytes unavailable\n");
		ok = 0;
	}
	else
	{
		emit("DEBT_SYSFS_RX_BYTES_OK ");
		emit(buf);
		if (buf[0] && buf[strlen(buf) - 1] != '\n')
			emit("\n");
	}

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
	{
		emit("DEBT_PROBE_FAIL socket\n");
		ok = 0;
		goto out;
	}
	memset(&ifc, 0, sizeof(ifc));
	ifc.ifc_len = (int)sizeof(ifcbuf);
	ifc.ifc_buf = ifcbuf;
	if (ioctl(sock, SIOCGIFCONF, &ifc) != 0)
	{
		emit("DEBT_PROBE_FAIL SIOCGIFCONF\n");
		ok = 0;
	}
	else if (ifc.ifc_len < (int)sizeof(struct ifreq))
	{
		emit("DEBT_PROBE_FAIL SIOCGIFCONF empty\n");
		ok = 0;
	}
	else
	{
		struct ifreq *ifr = (struct ifreq *)ifcbuf;

		emit("DEBT_SIOCGIFCONF_OK ");
		emit(ifr->ifr_name);
		emit("\n");
	}
	close(sock);

out:
	if (ok)
		emit("DEBT_PROBE_PASS\n");
	else
		emit("DEBT_PROBE_FAIL\n");
	for (;;)
		pause();
	return 0;
}
