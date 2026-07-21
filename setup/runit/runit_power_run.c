/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: runit_power_run.c
 * Description: runit service run stub that execs /bin/power-smoke.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <unistd.h>
#include "ir0_smoke_tag.h"

#ifndef RUNIT_EXEC_PATH
#define RUNIT_EXEC_PATH "/bin/power-smoke"
#endif

#ifndef RUNIT_START_TAG
#define RUNIT_START_TAG "RUNSV_POWER_START\n"
#endif


int main(void)
{
	ir0_smoke_tag(RUNIT_START_TAG);
	execl(RUNIT_EXEC_PATH, RUNIT_EXEC_PATH, (char *)0);
	ir0_smoke_tag("RUNSV_POWER_EXEC_FAIL\n");
	return 1;
}
