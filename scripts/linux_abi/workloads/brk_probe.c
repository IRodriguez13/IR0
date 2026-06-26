/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: brk_probe.c
 * Description: Minimal brk(2) workload for Linux↔IR0 ABI audit (same static ELF)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#define BRK_GROW_BYTES 0x1000UL

static void audit_brk(unsigned step, long ret, int err)
{
	char buf[128];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "[LINUX_ABI_AUDIT][brk] step=%u ret=%ld errno=%d\n",
		     step, ret, err);
	if (n > 0)
		(void)write(1, buf, (size_t)n);
}

int main(void)
{
	uintptr_t cur;
	uintptr_t target;
	long ret;

	ret = (long)syscall(SYS_brk, 0);
	cur = (uintptr_t)ret;
	audit_brk(0, ret, ret == (long)-1 ? errno : 0);

	target = cur + BRK_GROW_BYTES;
	ret = (long)syscall(SYS_brk, (void *)target);
	audit_brk(1, ret, ret == (long)-1 ? errno : 0);

	if (ret != (long)target)
		return 1;

	(void)write(1, "[BRKOK]\n", 8);
	return 0;
}
