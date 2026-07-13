/**
 * IR0 — hostshare exec stub (MINIX /sbin/init)
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: init_hostshare_exec.c
 * Description: Mount virtio-9p at /mnt/host and execve /mnt/host/ir0_payload.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#define PAYLOAD_PATH "/mnt/host/ir0_payload"

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

int main(int argc, char **argv, char **envp)
{
	char *payload_argv[2];

	(void)argc;
	(void)argv;

	(void)mkdir("/mnt", 0755);
	(void)mkdir("/mnt/host", 0755);

	if (mount("ir0share", "/mnt/host", "9p", 0, NULL) != 0)
	{
		say("HOSTSHARE_EXEC_MOUNT_FAIL\n");
		return 2;
	}
	say("HOSTSHARE_EXEC_MOUNT_OK\n");

	payload_argv[0] = (char *)PAYLOAD_PATH;
	payload_argv[1] = NULL;
	execve(PAYLOAD_PATH, payload_argv, envp);
	say("HOSTSHARE_EXEC_EXEC_FAIL errno=");
	{
		char buf[16];
		int e = errno;
		int i = 0;
		if (e == 0)
			buf[i++] = '0';
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
		say(buf);
	}
	say("\n");
	return 3;
}
