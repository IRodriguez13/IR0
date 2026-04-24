/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: ktest
 * Runs the in-kernel test suite (syscall/procfs/process coverage).
 *
 * This command is intentionally test-only and linked only with
 * IR0_KERNEL_TESTS builds.
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
