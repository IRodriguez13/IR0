/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: runit_exec_run.c
 * Description: IR0 kernel source — runit exec run
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <unistd.h>

#ifndef RUNIT_EXEC_PATH
#define RUNIT_EXEC_PATH "/bin/sh"
#endif

#ifndef RUNIT_START_TAG
#define RUNIT_START_TAG "RUNSV_EXEC_START\n"
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
	char *const argv[] = { (char *)RUNIT_EXEC_PATH, NULL };

	tag(RUNIT_START_TAG);
	execv(RUNIT_EXEC_PATH, argv);
	tag("RUNSV_EXEC_FAIL\n");
	return 111;
}
