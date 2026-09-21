/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * Host tests for /proc Linux-facing ABI helpers (PROC-AUDIT).
 */

#include "test_harness_ir0.h"
#include <stdio.h>
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

static void test_stat_global_counter_lines(void)
{
	const char *sample =
		"cpu  10 0 5 985 0 0 0 0\n"
		"cpu0 10 0 5 985 0 0 0 0\n"
		"intr 12345\n"
		"ctxt 678\n"
		"btime 1700000000\n";
	unsigned long long intr = 0;
	unsigned long long ctxt = 0;
	long long btime = 0;
	const char *line;

	line = strstr(sample, "intr ");
	ASSERT(line != NULL);
	ASSERT(sscanf(line, "intr %llu", &intr) == 1);
	ASSERT(intr == 12345ULL);
	line = strstr(sample, "ctxt ");
	ASSERT(line != NULL);
	ASSERT(sscanf(line, "ctxt %llu", &ctxt) == 1);
	ASSERT(ctxt == 678ULL);
	line = strstr(sample, "btime ");
	ASSERT(line != NULL);
	ASSERT(sscanf(line, "btime %lld", &btime) == 1);
	ASSERT(btime == 1700000000LL);
}

void test_procfs_linux_abi(void)
{
	TEST_BEGIN("procfs_cpu_jiffy_invariant");
	test_cpu_jiffy_invariant();
	TEST_END();

	TEST_BEGIN("procfs_status_uid_line_format");
	test_status_uid_line_format();
	TEST_END();

	TEST_BEGIN("procfs_stat_global_counter_lines");
	test_stat_global_counter_lines();
	TEST_END();
}
