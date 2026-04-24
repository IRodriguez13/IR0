/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_ktest.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: ktest
 * Ejecuta la batería de tests in-kernel (syscalls, procfs, process, etc.).
 */

#include "debug_bins.h"
#include "test/test_runner.h"
#include <string.h>

static int cmd_ktest_handler(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	debug_writeln("[ktest] Running kernel test suite...");
	kernel_test_run_all();
	debug_serial_ok("ktest");
	return 0;
}

struct debug_command cmd_ktest = {
	.name = "ktest",
	.handler = cmd_ktest_handler,
	.usage = "ktest",
	.description = "Run in-kernel test suite (exhaustive)"
};
