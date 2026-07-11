/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: init_kexec_load_smoke.c
 * Description: kexec_load magic payload + reboot(KEXEC) → REBOOT_KEXEC_LOADED.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include <string.h>
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
#ifndef __NR_kexec_load
#define __NR_kexec_load 246
#endif
#ifndef KEXEC_ARCH_DEFAULT
#define KEXEC_ARCH_DEFAULT 0ul
#endif

struct kexec_segment
{
	void *buf;
	size_t bufsz;
	void *mem;
	size_t memsz;
};

static void tag(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

int main(void)
{
	char payload[32];
	struct kexec_segment seg;
	long r;

	memset(payload, 0, sizeof(payload));
	memcpy(payload, "IR0KEXEC", 8);

	seg.buf = payload;
	seg.bufsz = sizeof(payload);
	seg.mem = (void *)0;
	seg.memsz = sizeof(payload);

	tag("KEXEC_LOAD_SMOKE_CALL\n");
	r = syscall(__NR_kexec_load, 0ul, 1ul, &seg, KEXEC_ARCH_DEFAULT);
	if (r != 0)
	{
		tag("KEXEC_LOAD_SMOKE_FAIL\n");
		return 1;
	}

	(void)syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		      (unsigned int)LINUX_REBOOT_CMD_KEXEC, (void *)0);
	tag("KEXEC_LOAD_SMOKE_RETURN\n");
	return 1;
}
