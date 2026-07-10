/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: runit_fase55d_init.c
 * Description: FASE55D smoke init — stage2 tag + doom service exec (no runsvdir spin)
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
	char *const argv[] = { "/bin/doom-smoke", NULL };

	tag("RUNIT_STAGE2_OK\n");
	tag("RUNSV_FASE55D_START\n");
	execv("/bin/doom-smoke", argv);
	tag("FASE55D_INIT_EXEC_FAIL\n");
	return 111;
}
