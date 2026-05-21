/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: ktest
 * Runs the in-kernel test suite (syscall/procfs/process coverage).
 *
 * IR0_KERNEL_TESTS guard (Makefile + debug_bins_registry.c):
 * - Default kernel (make ir0 / kernel-x64.bin): IR0_KERNEL_TESTS is
 *   undefined; cmd_ktest.o is not linked and the command is not registered.
 * - Test kernel (make tests / kernel-x64-test.bin): Makefile passes
 *   -DIR0_KERNEL_TESTS=1, links cmd_ktest.o, and registers cmd_ktest in
 *   debug_bins_registry_test.o only under #ifdef IR0_KERNEL_TESTS.
 * - Do not invoke kernel_test_run_all() from non-test builds.
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
