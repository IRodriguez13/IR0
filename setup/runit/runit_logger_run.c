/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: runit_logger_run.c
 * Description: IR0 kernel source — runit logger run
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <unistd.h>

static void tag(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

int main(void)
{
	tag("RUNSV_LOGGER_START\n");

	for (;;)
		(void)pause();
}
