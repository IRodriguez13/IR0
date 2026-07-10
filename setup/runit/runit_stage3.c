/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: runit_stage3.c
 * Description: runit stage 3 — sync + halt (ordered shutdown path).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <unistd.h>
#include <sys/syscall.h>
#include <sys/reboot.h>

#ifndef LINUX_REBOOT_MAGIC1
#define LINUX_REBOOT_MAGIC1 0xfee1dead
#endif
#ifndef LINUX_REBOOT_MAGIC2
#define LINUX_REBOOT_MAGIC2 672274793
#endif
#ifndef LINUX_REBOOT_CMD_HALT
#define LINUX_REBOOT_CMD_HALT 0xCDEF0123u
#endif

static void tag(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

int main(void)
{
	tag("RUNIT_STAGE3_OK\n");
	(void)syscall(SYS_sync);
	tag("RUNIT_STAGE3_SYNC_DONE\n");
	(void)syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		      (unsigned int)LINUX_REBOOT_CMD_HALT, (void *)0);
	tag("RUNIT_STAGE3_REBOOT_RETURNED\n");
	return 1;
}
