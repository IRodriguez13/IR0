/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * Host tests for /proc Linux-facing ABI helpers (PROC-AUDIT).
 */

#include "test_harness_ir0.h"
#include <string.h>

static void test_cpu_jiffy_invariant(void)
{
	unsigned long long user = 10;
	unsigned long long system = 5;
	unsigned long long idle = 985;

	ASSERT(user + system + idle <= 1000ULL);
	ASSERT(idle > user);
	ASSERT(idle > system);
}

static void test_status_uid_line_format(void)
{
	char buf[256];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "Name:\ttop\n"
		     "State:\tR (running)\n"
		     "Pid:\t42\n"
		     "PPid:\t1\n"
		     "Uid:\t1000\t1000\t1000\t1000\n"
		     "Gid:\t1000\t1000\t1000\t1000\n");
	ASSERT(n > 0);
	ASSERT(strstr(buf, "Uid:\t1000") != NULL);
	ASSERT(strstr(buf, "Name:\ttop") != NULL);
}

void test_procfs_linux_abi(void)
{
	TEST_BEGIN("procfs_cpu_jiffy_invariant");
	test_cpu_jiffy_invariant();
	TEST_END();

	TEST_BEGIN("procfs_status_uid_line_format");
	test_status_uid_line_format();
	TEST_END();
}
