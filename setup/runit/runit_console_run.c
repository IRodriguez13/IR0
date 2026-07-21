/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: runit_console_run.c
 * Description: IR0 kernel source — runit console run
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <fcntl.h>
#include <unistd.h>
#include "ir0_smoke_tag.h"


int main(void)
{
	int fd;
	char *const argv[] = { "/bin/sh", NULL };

	ir0_smoke_tag("RUNSV_CONSOLE_START\n");

	fd = open("/dev/console", O_RDWR);
	if (fd >= 0)
	{
		(void)dup2(fd, 0);
		(void)dup2(fd, 1);
		(void)dup2(fd, 2);
		if (fd > 2)
			(void)close(fd);
		ir0_smoke_tag("ASH_INTERACTIVE_READY\n");
	}

	execv("/bin/sh", argv);
	ir0_smoke_tag("RUNSV_CONSOLE_EXEC_FAIL\n");
	return 111;
}
