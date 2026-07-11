/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: init_reboot_kexec_enosys_smoke.c
 * Description: reboot(KEXEC) must return -ENOSYS (stub; no kexec_load).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef LINUX_REBOOT_MAGIC1
#define LINUX_REBOOT_MAGIC1 0xfee1dead
#endif
#ifndef LINUX_REBOOT_MAGIC2
#define LINUX_REBOOT_MAGIC2 672274793
#endif
#ifndef LINUX_REBOOT_CMD_KEXEC
#define LINUX_REBOOT_CMD_KEXEC 0x45584543u
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

	tag("KEXEC_SMOKE_CALL\n");
	r = syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		    (unsigned int)LINUX_REBOOT_CMD_KEXEC, (void *)0);
	if (r == -1 && errno == ENOSYS)
	{
		tag("KEXEC_ENOSYS_OK\n");
		return 0;
	}
	tag("KEXEC_SMOKE_FAIL\n");
	return 1;
}
