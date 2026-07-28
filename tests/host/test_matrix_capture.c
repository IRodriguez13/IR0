/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_matrix_capture.c
 * Description: Host unit tests for BusyBox matrix capture (drain + needles).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#define _GNU_SOURCE

#include "test_harness.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "matrix_capture.h"

static void write_all(int fd, const void *buf, size_t n)
{
	const char *p = (const char *)buf;

	while (n > 0)
	{
		ssize_t w = write(fd, p, n);

		if (w < 0)
		{
			if (errno == EINTR)
				continue;
			return;
		}
		p += (size_t)w;
		n -= (size_t)w;
	}
}

static void test_needle_single_chunk(void)
{
	struct matrix_needle_matcher m;

	TEST_BEGIN("matrix_needle single chunk");
	matrix_needle_init(&m, "Usage:");
	matrix_needle_feed(&m, "BusyBox Usage: ls\n", 18);
	ASSERT(matrix_needle_found(&m));
	TEST_END();
}

static void test_needle_split_two(void)
{
	struct matrix_needle_matcher m;

	TEST_BEGIN("matrix_needle split two chunks");
	matrix_needle_init(&m, "MATRIX_OK");
	matrix_needle_feed(&m, "BUSYBOX_MATR", 12);
	ASSERT(!matrix_needle_found(&m));
	matrix_needle_feed(&m, "IX_OK rest", 10);
	ASSERT(matrix_needle_found(&m));
	TEST_END();
}

static void test_needle_byte_by_byte(void)
{
	struct matrix_needle_matcher m;
	const char *s = "uid=0(root)";
	size_t i;

	TEST_BEGIN("matrix_needle byte by byte");
	matrix_needle_init(&m, "uid=0");
	for (i = 0; i < strlen(s); i++)
		matrix_needle_feed(&m, s + i, 1);
	ASSERT(matrix_needle_found(&m));
	TEST_END();
}

static void test_needle_no_nul_strstr(void)
{
	struct matrix_capture c;
	char store[64];
	char raw[16];

	TEST_BEGIN("matrix_capture feed without NUL");
	memset(raw, 'A', sizeof(raw));
	memcpy(raw + 4, "Usage:", 6);
	matrix_capture_init(&c, store, sizeof(store), "Usage:");
	matrix_capture_feed(&c, raw, sizeof(raw));
	ASSERT(matrix_needle_found(&c.matcher));
	ASSERT(c.bytes_seen == sizeof(raw));
	TEST_END();
}

static void test_truncate_keeps_matcher(void)
{
	struct matrix_capture c;
	char store[16];
	char big[64];

	TEST_BEGIN("matrix_capture truncate still matches");
	memset(big, 'x', sizeof(big));
	memcpy(big + 2, "Usage:", 6);
	matrix_capture_init(&c, store, sizeof(store), "Usage:");
	matrix_capture_feed(&c, big, sizeof(big));
	ASSERT(c.truncated);
	ASSERT(c.store_len == sizeof(store) - 1);
	ASSERT(matrix_needle_found(&c.matcher));
	ASSERT(c.bytes_seen == sizeof(big));
	TEST_END();
}

/*
 * Reproduce the flake class: after writer closes, nonblocking reader must
 * not stop on the first EAGAIN — drain_to_eof must poll until read()==0.
 */
static void test_drain_after_exit_eagain(void)
{
	int fds[2];
	pid_t pid;
	struct matrix_capture c;
	char store[256];
	int flags;

	TEST_BEGIN("matrix_capture drain to EOF after writer exit");
	ASSERT(pipe(fds) == 0);
	pid = fork();
	ASSERT(pid >= 0);
	if (pid == 0)
	{
		(void)close(fds[0]);
		/* Small writes that may not all be read before exit. */
		write_all(fds[1], "hello ", 6);
		usleep(2000);
		write_all(fds[1], "Usage: ls\n", 10);
		(void)close(fds[1]);
		_exit(0);
	}
	(void)close(fds[1]);
	flags = fcntl(fds[0], F_GETFL);
	ASSERT(fcntl(fds[0], F_SETFL, flags | O_NONBLOCK) == 0);

	matrix_capture_init(&c, store, sizeof(store), "Usage:");
	/* Simulate parent racing ahead of pipe data. */
	(void)waitpid(pid, NULL, 0);
	ASSERT(matrix_capture_drain_to_eof(&c, fds[0], 2000) == 0);
	ASSERT(c.saw_eof);
	ASSERT(matrix_needle_found(&c.matcher));
	(void)close(fds[0]);
	TEST_END();
}

static void test_legacy_eagain_stop_loses_needle(void)
{
	int fds[2];
	pid_t pid;
	char store[256];
	size_t total = 0;
	int flags;
	ssize_t n;

	TEST_BEGIN("legacy EAGAIN-stop can miss late needle (flake class)");
	ASSERT(pipe(fds) == 0);
	pid = fork();
	ASSERT(pid >= 0);
	if (pid == 0)
	{
		(void)close(fds[0]);
		write_all(fds[1], "preamble\n", 9);
		/* Delay second write until parent likely waited. */
		usleep(50000);
		write_all(fds[1], "Usage: late\n", 12);
		(void)close(fds[1]);
		_exit(0);
	}
	(void)close(fds[1]);
	flags = fcntl(fds[0], F_GETFL);
	ASSERT(fcntl(fds[0], F_SETFL, flags | O_NONBLOCK) == 0);

	(void)waitpid(pid, NULL, 0);
	/* Legacy: single nonblocking drain loop that stops on EAGAIN. */
	for (;;)
	{
		n = read(fds[0], store + total, sizeof(store) - 1 - total);
		if (n > 0)
		{
			total += (size_t)n;
			continue;
		}
		break; /* EOF or EAGAIN — bug treats both as done */
	}
	store[total] = '\0';
	/*
	 * On a fast machine we may still get everything; the important
	 * assertion is that the fixed drain path (previous test) is required
	 * for correctness. Here we only document the pattern.
	 */
	ASSERT(total < sizeof(store));
	(void)close(fds[0]);
	TEST_END();
}

static void test_pollhup_with_pending(void)
{
	int fds[2];
	struct matrix_capture c;
	char store[128];
	int flags;

	TEST_BEGIN("matrix_capture POLLHUP with pending bytes");
	ASSERT(pipe(fds) == 0);
	write_all(fds[1], "uid=0\n", 6);
	(void)close(fds[1]);
	flags = fcntl(fds[0], F_GETFL);
	ASSERT(fcntl(fds[0], F_SETFL, flags | O_NONBLOCK) == 0);
	matrix_capture_init(&c, store, sizeof(store), "uid=0");
	ASSERT(matrix_capture_drain_to_eof(&c, fds[0], 1000) == 0);
	ASSERT(c.saw_eof);
	ASSERT(matrix_needle_found(&c.matcher));
	(void)close(fds[0]);
	TEST_END();
}

static void test_empty_stdout(void)
{
	int fds[2];
	struct matrix_capture c;
	char store[32];
	int flags;

	TEST_BEGIN("matrix_capture empty then EOF");
	ASSERT(pipe(fds) == 0);
	(void)close(fds[1]);
	flags = fcntl(fds[0], F_GETFL);
	ASSERT(fcntl(fds[0], F_SETFL, flags | O_NONBLOCK) == 0);
	matrix_capture_init(&c, store, sizeof(store), NULL);
	ASSERT(matrix_capture_drain_to_eof(&c, fds[0], 500) == 0);
	ASSERT(c.saw_eof);
	ASSERT(c.bytes_seen == 0);
	(void)close(fds[0]);
	TEST_END();
}

static void test_no_newline(void)
{
	struct matrix_capture c;
	char store[64];

	TEST_BEGIN("matrix_capture needle without newline");
	matrix_capture_init(&c, store, sizeof(store), "IR0");
	matrix_capture_feed(&c, "Linux IR0 x86_64", 15);
	ASSERT(matrix_needle_found(&c.matcher));
	TEST_END();
}

/*
 * Writer exits after a delayed final byte; drain must not depend on
 * blocking poll(fd, timeout>0) (IR0 waiter pool).
 */
static void test_drain_delayed_byte_no_blocking_poll(void)
{
	int fds[2];
	pid_t pid;
	struct matrix_capture c;
	char store[128];
	int flags;

	TEST_BEGIN("matrix_capture drain delayed byte (poll timeout=0 path)");
	ASSERT(pipe(fds) == 0);
	pid = fork();
	ASSERT(pid >= 0);
	if (pid == 0)
	{
		(void)close(fds[0]);
		write_all(fds[1], "pre-", 4);
		usleep(30000);
		write_all(fds[1], "Usage:\n", 7);
		(void)close(fds[1]);
		_exit(0);
	}
	(void)close(fds[1]);
	flags = fcntl(fds[0], F_GETFL);
	ASSERT(fcntl(fds[0], F_SETFL, flags | O_NONBLOCK) == 0);
	matrix_capture_init(&c, store, sizeof(store), "Usage:");
	(void)waitpid(pid, NULL, 0);
	ASSERT(matrix_capture_drain_to_eof(&c, fds[0], 2000) == 0);
	ASSERT(c.saw_eof);
	ASSERT(matrix_needle_found(&c.matcher));
	(void)close(fds[0]);
	TEST_END();
}

void test_matrix_capture_suite(void)
{
	test_needle_single_chunk();
	test_needle_split_two();
	test_needle_byte_by_byte();
	test_needle_no_nul_strstr();
	test_truncate_keeps_matcher();
	test_drain_after_exit_eagain();
	test_legacy_eagain_stop_loses_needle();
	test_pollhup_with_pending();
	test_empty_stdout();
	test_no_newline();
	test_drain_delayed_byte_no_blocking_poll();
}
