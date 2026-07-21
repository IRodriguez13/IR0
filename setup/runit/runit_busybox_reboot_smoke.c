/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: runit_busybox_reboot_smoke.c
 * Description: BusyBox reboot applet → sys_reboot(RESTART).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <unistd.h>
#include "ir0_smoke_tag.h"
#include <sys/syscall.h>
#include <sys/reboot.h>

#ifndef LINUX_REBOOT_MAGIC1
#define LINUX_REBOOT_MAGIC1 0xfee1dead
#endif
#ifndef LINUX_REBOOT_MAGIC2
#define LINUX_REBOOT_MAGIC2 672274793
#endif
#ifndef LINUX_REBOOT_CMD_RESTART
#define LINUX_REBOOT_CMD_RESTART 0x01234567u
#endif


int main(void)
{
	ir0_smoke_tag("BUSYBOX_REBOOT_SMOKE_CALL\n");
	(void)syscall(SYS_sync);
	execl("/bin/reboot", "reboot", "-f", (char *)0);
	(void)syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		      (unsigned int)LINUX_REBOOT_CMD_RESTART, (void *)0);
	ir0_smoke_tag("BUSYBOX_REBOOT_SMOKE_RETURNED\n");
	return 1;
}
