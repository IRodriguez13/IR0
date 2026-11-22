/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * File: sched_lat.c
 * Description: Reports wakeup latency for runnable and blocking wakers.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
typedef unsigned long long uint64_t;
#else
#include <signal.h>
#include <stdint.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

#include "ir0/ksft.h"

#define LAT_N 20
#define LAT_WARMUP 2
#define LAT_SPIN_MS 15

static uint64_t now_ms(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0;
	return (uint64_t)ts.tv_sec * 1000ull +
	       (uint64_t)ts.tv_nsec / 1000000ull;
}

static void spin_until(uint64_t deadline)
{
	while (now_ms() < deadline)
		;
}

static void pack_u64(unsigned char raw[8], uint64_t value)
{
	int i;

	for (i = 0; i < 8; i++)
		raw[i] = (unsigned char)((value >> (8 * i)) & 0xffu);
}

static uint64_t unpack_u64(const unsigned char raw[8])
{
	uint64_t value = 0;
	int i;

	for (i = 0; i < 8; i++)
		value |= (uint64_t)raw[i] << (8 * i);
	return value;
}

static int read_full(int fd, void *buf, int n)
{
	unsigned char *p = buf;
	int got = 0;

	while (got < n)
	{
		int r = (int)read(fd, p + got, (size_t)(n - got));

		if (r <= 0)
			return -1;
		got += r;
	}
	return 0;
}

static int write_full(int fd, const void *buf, int n)
{
	const unsigned char *p = buf;
	int put = 0;

	while (put < n)
	{
		int r = (int)write(fd, p + put, (size_t)(n - put));

		if (r <= 0)
			return -1;
		put += r;
	}
	return 0;
}

static int open_pipe(int fds[2])
{
#ifdef IR0_SELFTEST_NOLIBC
	return pipe2(fds, 0);
#else
	return pipe(fds);
#endif
}

/* The child blocks in read. The parent wakes it, then stays runnable or blocks. */
static void child_loop(int wake_r, int res_w)
{
	for (;;)
	{
		unsigned char byte;
		unsigned char raw[8];
		uint64_t stamp;

		if (read_full(wake_r, &byte, 1) != 0)
			_exit(0);
		stamp = now_ms();
		pack_u64(raw, stamp);
		if (write_full(res_w, raw, 8) != 0)
			_exit(1);
	}
}

static int one_sample(int wake_w, int res_r, int spin, uint64_t *lat)
{
	unsigned char byte = 1;
	unsigned char raw[8];
	uint64_t t0;
	uint64_t stamp;

	t0 = now_ms();
	if (t0 == 0 || write_full(wake_w, &byte, 1) != 0)
		return -1;
	if (spin)
		spin_until(t0 + LAT_SPIN_MS);
	if (read_full(res_r, raw, 8) != 0)
		return -1;
	stamp = unpack_u64(raw);
	if (stamp < t0)
		return -1;
	*lat = stamp - t0;
	return 0;
}

static void sort_u64(uint64_t *values, int n)
{
	int i;

	for (i = 1; i < n; i++)
	{
		uint64_t key = values[i];
		int j = i;

		while (j > 0 && values[j - 1] > key)
		{
			values[j] = values[j - 1];
			j--;
		}
		values[j] = key;
	}
}

static int collect(int wake_w, int res_r, int spin, uint64_t *min_ms,
	uint64_t *p50_ms, uint64_t *max_ms, uint64_t *ge10)
{
	uint64_t samples[LAT_N];
	int i;
	int n = 0;

	*ge10 = 0;
	for (i = 0; i < LAT_WARMUP + LAT_N; i++)
	{
		uint64_t lat;

		if (one_sample(wake_w, res_r, spin, &lat) != 0)
			return -1;
		if (i < LAT_WARMUP)
			continue;
		samples[n++] = lat;
		if (lat >= 10)
			(*ge10)++;
	}
	sort_u64(samples, n);
	*min_ms = samples[0];
	*p50_ms = samples[n / 2];
	*max_ms = samples[n - 1];
	return 0;
}

int main(void)
{
	int wake[2];
	int res[2];
	int child;
	uint64_t wake_min;
	uint64_t wake_p50;
	uint64_t wake_max;
	uint64_t wake_ge10;
	uint64_t block_min;
	uint64_t block_p50;
	uint64_t block_max;
	uint64_t block_ge10;
	int pass = 0;

	ksft_set_plan(1);
	if (open_pipe(wake) != 0 || open_pipe(res) != 0)
		goto done;
	child = fork();
	if (child < 0)
		goto done;
	if (child == 0)
	{
		(void)close(wake[1]);
		(void)close(res[0]);
		child_loop(wake[0], res[1]);
	}
	(void)close(wake[0]);
	(void)close(res[1]);
	if (collect(wake[1], res[0], 1, &wake_min, &wake_p50, &wake_max,
		&wake_ge10) != 0)
		goto reap;
	if (collect(wake[1], res[0], 0, &block_min, &block_p50, &block_max,
		&block_ge10) != 0)
		goto reap;
	ksft_print_num("wake_min_ms", (long)wake_min);
	ksft_print_num("wake_p50_ms", (long)wake_p50);
	ksft_print_num("wake_max_ms", (long)wake_max);
	ksft_print_num("wake_ge10", (long)wake_ge10);
	ksft_print_num("block_min_ms", (long)block_min);
	ksft_print_num("block_p50_ms", (long)block_p50);
	ksft_print_num("block_max_ms", (long)block_max);
	ksft_print_num("block_ge10", (long)block_ge10);
	pass = 1;
reap:
	(void)close(wake[1]);
	(void)close(res[0]);
	(void)kill(child, SIGKILL);
	{
		int status;

#ifdef IR0_SELFTEST_NOLIBC
		(void)wait4(child, &status, 0, 0);
#else
		(void)waitpid(child, &status, 0);
#endif
	}
done:
	ksft_test_result(pass, "sched_lat");
	return ksft_exit();
}
