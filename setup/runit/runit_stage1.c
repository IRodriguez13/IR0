/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: runit_stage1.c
 * Description: IR0 kernel source — runit stage1
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <unistd.h>
#include "ir0_smoke_tag.h"


int main(void)
{
	char *const argv[] = { "/etc/runit/2", NULL };

	ir0_smoke_tag("RUNIT_STAGE1_OK\n");
	execv("/etc/runit/2", argv);
	ir0_smoke_tag("RUNIT_STAGE1_EXEC_FAIL\n");
	return 111;
}
