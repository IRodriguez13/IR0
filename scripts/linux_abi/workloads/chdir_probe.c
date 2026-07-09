/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: chdir_probe.c
 * Description: chdir(2) + getcwd verify for Linux↔IR0 ABI audit
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef CHDIR_TARGET
#define CHDIR_TARGET "/"
#endif

static void audit_chdir(unsigned step, const char *op, long ret, int err,
			const char *path)
{
	char buf[256];
	int n;

	if (path && path[0])
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][chdir] step=%u op=%s ret=%ld errno=%d path=%s\n",
			     step, op, ret, err, path);
	}
	else
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][chdir] step=%u op=%s ret=%ld errno=%d\n",
			     step, op, ret, err);
	}
	if (n > 0)
		(void)write(1, buf, (size_t)n);
}

int main(void)
{
	char cwd[128];
	char *p;
	int rc;

	rc = chdir(CHDIR_TARGET);
	audit_chdir(0, "chdir_target", (long)rc, rc < 0 ? errno : 0, CHDIR_TARGET);
	if (rc != 0)
		return 1;

	memset(cwd, 0, sizeof(cwd));
	p = getcwd(cwd, sizeof(cwd));
	audit_chdir(1, "getcwd_after", p ? (long)strlen(cwd) : -1L,
		    p ? 0 : errno, p ? cwd : NULL);
	if (!p || strcmp(cwd, CHDIR_TARGET) != 0)
		return 1;

	(void)write(1, "[CHDIROK]\n", 10);
	return 0;
}
