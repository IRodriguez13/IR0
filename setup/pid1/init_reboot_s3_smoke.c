/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: init_reboot_s3_smoke.c
 * Description: reboot(SW_SUSPEND) → S3 enter/resume tags + userspace OK.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <sys/syscall.h>
#include <unistd.h>

#ifndef LINUX_REBOOT_MAGIC1
#define LINUX_REBOOT_MAGIC1 0xfee1dead
#endif
#ifndef LINUX_REBOOT_MAGIC2
#define LINUX_REBOOT_MAGIC2 672274793
#endif
#ifndef LINUX_REBOOT_CMD_SW_SUSPEND
#define LINUX_REBOOT_CMD_SW_SUSPEND 0xD000FCE2u
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
	long r;

	tag("S3_SMOKE_CALL\n");
	r = syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		    (unsigned int)LINUX_REBOOT_CMD_SW_SUSPEND, (void *)0);
	if (r == 0)
	{
		tag("SUSPEND_S3_OK\n");
		return 0;
	}
	tag("S3_SMOKE_FAIL\n");
	return 1;
}
