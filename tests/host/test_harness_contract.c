/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_harness_contract.c
 * Description: Contract tests for the host assertion macros themselves.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"

#include <stdint.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define KTEST_HARNESS_SENTINEL 0x123456789ABCDEF0ULL

static int g_operand_calls;

static uint64_t side_effect_value(void)
{
	g_operand_calls++;
	return KTEST_HARNESS_SENTINEL;
}

/*
 * Assertions only print their operands on failure. Run the failing case in
 * a child so the parent suite stays green while we inspect the diagnostic.
 */
static int run_assertion_child(void (*fn)(void), char *output, size_t out_len)
{
	int fds[2];
	pid_t pid;
	ssize_t n;
	int status = 0;

	output[0] = '\0';
	if (pipe(fds) != 0)
		return -1;

	pid = fork();
	if (pid < 0)
	{
		close(fds[0]);
		close(fds[1]);
		return -1;
	}
	if (pid == 0)
	{
		close(fds[0]);
		if (dup2(fds[1], STDERR_FILENO) < 0)
			_exit(9);
		close(fds[1]);
		fn();
		_exit(0);
	}

	close(fds[1]);
	n = read(fds[0], output, out_len - 1);
	if (n > 0)
		output[n] = '\0';
	close(fds[0]);

	if (waitpid(pid, &status, 0) != pid)
		return -1;
	if (!WIFEXITED(status))
		return -1;
	return WEXITSTATUS(status);
}

static void child_eq_failure(void)
{
	g_operand_calls = 0;
	ASSERT_EQ(side_effect_value(), side_effect_value() + 1);
	/* Two operands, one evaluation each: four calls mean re-evaluation. */
	if (g_operand_calls != 2)
		_exit(3);
	_exit(_ir0_test_failed ? 0 : 4);
}

static void child_ne_failure(void)
{
	g_operand_calls = 0;
	ASSERT_NE(side_effect_value(), KTEST_HARNESS_SENTINEL);
	if (g_operand_calls != 1)
		_exit(3);
	_exit(_ir0_test_failed ? 0 : 4);
}

static void child_str_eq_null(void)
{
	ASSERT_STR_EQ(NULL, "not-null");
	_exit(_ir0_test_failed ? 0 : 4);
}

void test_harness_assertion_contract(void)
{
	char output[512];
	int status;

	TEST_BEGIN("host harness evaluates operands once and keeps 64 bits");

	status = run_assertion_child(child_eq_failure, output, sizeof(output));
	ASSERT_EQ(status, 0);
	ASSERT(strstr(output, "ASSERT_EQ failed") != NULL);
	/* The full 64-bit values must survive the failure report. */
	ASSERT(strstr(output, "123456789abcdef0") != NULL);
	ASSERT(strstr(output, "123456789abcdef1") != NULL);

	status = run_assertion_child(child_ne_failure, output, sizeof(output));
	ASSERT_EQ(status, 0);
	ASSERT(strstr(output, "ASSERT_NE failed") != NULL);

	/* A NULL string operand must fail the assertion, not crash. */
	status = run_assertion_child(child_str_eq_null, output, sizeof(output));
	ASSERT_EQ(status, 0);
	ASSERT(strstr(output, "ASSERT_STR_EQ failed") != NULL);
	ASSERT(strstr(output, "(null)") != NULL);

	TEST_END();
}
