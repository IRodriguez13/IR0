/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: runit_busybox_poweroff_smoke.c
 * Description: BusyBox poweroff applet → sys_reboot(POWER_OFF).
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
#ifndef LINUX_REBOOT_CMD_POWER_OFF
#define LINUX_REBOOT_CMD_POWER_OFF 0x4321FEDCu
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
	tag("BUSYBOX_POWEROFF_SMOKE_CALL\n");
	(void)syscall(SYS_sync);
	execl("/bin/poweroff", "poweroff", "-f", (char *)0);
	(void)syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		      (unsigned int)LINUX_REBOOT_CMD_POWER_OFF, (void *)0);
	tag("BUSYBOX_POWEROFF_SMOKE_RETURNED\n");
	return 1;
}
