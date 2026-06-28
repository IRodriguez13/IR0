/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_tty_canon_read.c
 * Description: D1.16 canonical TTY read block/wake ktests
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test/ktest_harness.h"
#include "process.h"
#include <ir0/console.h>
#include <ir0/scheduler_api.h>
#include <string.h>

extern void kernel_idle_poll(void);

static volatile int tty_ktest_done;
static volatile int64_t tty_ktest_result;
static char tty_ktest_buf[32];

static void tty_ktest_inject_line(const char *line)
{
	size_t i;

	for (i = 0; line[i]; i++)
		ir0_console_input_enqueue(line[i]);
}

static void ktest_tty_read_child(void)
{
	char kbuf[32];
	int64_t n;

	n = ir0_console_read(kbuf, sizeof(kbuf), 0);
	tty_ktest_result = n;
	if (n > 0 && (size_t)n <= sizeof(tty_ktest_buf))
		memcpy(tty_ktest_buf, kbuf, (size_t)n);
	tty_ktest_done = 1;
	process_exit(0);
}

void ktest_tty_canon_read_immediate(void)
{
	char kbuf[32];
	int64_t n;

	KTEST_BEGIN("tty_canon_read_immediate");

	ir0_console_flush_input();
	tty_ktest_inject_line("echo hi\n");

	n = ir0_console_read(kbuf, sizeof(kbuf), 0);
	KASSERT_EQ(n, 8);
	KASSERT(memcmp(kbuf, "echo hi\n", 8) == 0);

	KTEST_END();
}

void ktest_tty_canon_block_wake(void)
{
	pid_t pid;
	int i;

	KTEST_BEGIN("tty_canon_block_wake");

	ir0_console_flush_input();
	tty_ktest_done = 0;
	tty_ktest_result = -1;
	memset(tty_ktest_buf, 0, sizeof(tty_ktest_buf));

	pid = spawn_kernel(ktest_tty_read_child, "tty_read");
	KASSERT_GT(pid, 0);

	for (i = 0; i < 50000 && !ir0_console_has_blocked_reader(); i++)
		sched_schedule_next();

	KASSERT(ir0_console_has_blocked_reader());

	tty_ktest_inject_line("echo hi\n");

	for (i = 0; i < 200000 && !tty_ktest_done; i++)
	{
		sched_schedule_next();
		kernel_idle_poll();
	}

	KASSERT(tty_ktest_done);
	KASSERT_EQ(tty_ktest_result, 8);
	KASSERT(memcmp(tty_ktest_buf, "echo hi\n", 8) == 0);

	KTEST_END();
}
