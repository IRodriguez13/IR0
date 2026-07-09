/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: nanosleep_probe.c
 * Description: nanosleep(2) minimal workload for Linux↔IR0 ABI audit
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static void audit_nanosleep(unsigned step, const char *op, long ret, int err)
{
	char buf[128];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "[LINUX_ABI_AUDIT][nanosleep] step=%u op=%s ret=%ld errno=%d\n",
		     step, op, ret, err);
	if (n > 0)
		(void)write(1, buf, (size_t)n);
}

int main(void)
{
	struct timespec req = {0, 2000000L};
	long ret;

	ret = (long)nanosleep(&req, NULL);
	audit_nanosleep(0, "nanosleep_2ms", ret, ret < 0 ? errno : 0);
	if (ret != 0)
		return 1;

	(void)write(1, "[NANOSLEEPOK]\n", 14);
	return 0;
}
