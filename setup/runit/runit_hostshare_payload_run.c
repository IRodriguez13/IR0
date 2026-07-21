/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: runit_hostshare_payload_run.c
 * Description: runit supervised service — mount virtio-9p and exec share payload.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ir0_smoke_tag.h"

#define PAYLOAD_PATH "/mnt/host/ir0_payload"


static void say_errno(const char *prefix)
{
	char buf[16];
	int e = errno;
	int i = 0;

	ir0_smoke_tag(prefix);
	if (e == 0)
	{
		buf[i++] = '0';
	}
	else
	{
		char tmp[16];
		int n = 0;

		while (e > 0 && n < 15)
		{
			tmp[n++] = (char)('0' + (e % 10));
			e /= 10;
		}
		while (n > 0)
			buf[i++] = tmp[--n];
	}
	buf[i] = '\0';
	ir0_smoke_tag(buf);
	ir0_smoke_tag("\n");
}

int main(int argc, char **argv, char **envp)
{
	char *payload_argv[2];

	(void)argc;
	(void)argv;

	ir0_smoke_tag("RUNSV_HOSTSHARE_START\n");

	(void)mkdir("/mnt", 0755);
	(void)mkdir("/mnt/host", 0755);

	if (mount("ir0share", "/mnt/host", "9p", 0, NULL) != 0)
	{
		ir0_smoke_tag("HOSTSHARE_EXEC_MOUNT_FAIL\n");
		return 2;
	}
	ir0_smoke_tag("HOSTSHARE_EXEC_MOUNT_OK\n");

	payload_argv[0] = (char *)PAYLOAD_PATH;
	payload_argv[1] = NULL;
	execve(PAYLOAD_PATH, payload_argv, envp);
	say_errno("HOSTSHARE_EXEC_EXEC_FAIL errno=");
	return 3;
}
