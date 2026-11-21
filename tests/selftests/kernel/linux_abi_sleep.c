/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: linux_abi_sleep.c
 * Description: Selftest: clock_nanosleep(2) and sched_yield(2) as on Linux.
 *              Both returned ENOSYS on x86_64 and arm64 (dev 26f89a5b).
 *              Every case passes on Linux. Run by
 *              make ir0-selftest-sleep-abi-{x86,arm64}.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "conf.h"

#include <time.h>

#define LINUX_CLOCK_REALTIME 0
#define LINUX_CLOCK_MONOTONIC 1
#define LINUX_CLOCK_THREAD_CPUTIME_ID 3
#define LINUX_CLOCK_MONOTONIC_RAW 4
#define LINUX_CLOCK_BOOTTIME 7
#define LINUX_TIMER_ABSTIME 1
#define SLEEP_MS 100L
/* An idle guest wakes within this bound; it only catches "never slept". */
#define SLACK_MS 1000L

static volatile int g_hits;

static void on_signal(int sig)
{
	(void)sig;
	g_hits++;
}

static long ts_ms(const struct timespec *ts)
{
	return ts->tv_sec * 1000L + ts->tv_nsec / 1000000L;
}

static long clock_ms(int clock)
{
	struct timespec ts;

	if (S2(SYS_clock_gettime, clock, &ts) != 0)
		return -1;
	return ts_ms(&ts);
}

static void ts_set_ms(struct timespec *ts, long ms)
{
	ts->tv_sec = ms / 1000L;
	ts->tv_nsec = (ms % 1000L) * 1000000L;
}

/* clock_nanosleep(2): a relative sleep returns 0 after the full interval. */
int sleep_clock_nanosleep_relative(void)
{
	struct timespec req;
	long t0;
	long dt;

	ts_set_ms(&req, SLEEP_MS);
	t0 = rt_now_ms();
	EXPECT(S4(SYS_clock_nanosleep, LINUX_CLOCK_MONOTONIC, 0, &req, 0), 0);
	dt = rt_now_ms() - t0;
	CHECK(dt >= SLEEP_MS && dt < SLEEP_MS + SLACK_MS);

	ts_set_ms(&req, SLEEP_MS);
	t0 = rt_now_ms();
	EXPECT(S4(SYS_clock_nanosleep, LINUX_CLOCK_REALTIME, 0, &req, 0), 0);
	dt = rt_now_ms() - t0;
	CHECK(dt >= SLEEP_MS && dt < SLEEP_MS + SLACK_MS);
	return CONF_PASS;
}

/*
 * TIMER_ABSTIME: the call returns once the clock reached the requested
 * time; a time already past returns 0 at once.
 */
int sleep_clock_nanosleep_absolute(void)
{
	static const int clocks[] = { LINUX_CLOCK_MONOTONIC,
				      LINUX_CLOCK_REALTIME,
				      LINUX_CLOCK_BOOTTIME };
	struct timespec req;
	unsigned int i;
	long target;
	long t0;

	for (i = 0; i < sizeof(clocks) / sizeof(clocks[0]); i++)
	{
		target = clock_ms(clocks[i]);
		CHECK(target >= 0);
		target += SLEEP_MS;
		ts_set_ms(&req, target);
		EXPECT(S4(SYS_clock_nanosleep, clocks[i], LINUX_TIMER_ABSTIME,
			  &req, 0), 0);
		CHECK(clock_ms(clocks[i]) >= target);
	}

	ts_set_ms(&req, 1);
	t0 = rt_now_ms();
	EXPECT(S4(SYS_clock_nanosleep, LINUX_CLOCK_MONOTONIC,
		  LINUX_TIMER_ABSTIME, &req, 0), 0);
	CHECK(rt_now_ms() - t0 < SLACK_MS);
	return CONF_PASS;
}

/*
 * Linux clock_nanosleep(): a bad timespec is EINVAL, an unknown clock is
 * EINVAL, and a clock without nsleep (MONOTONIC_RAW, the calling thread's
 * CPU clock) is EOPNOTSUPP.
 */
int sleep_clock_nanosleep_bad_args(void)
{
	struct timespec req;

	req.tv_sec = 0;
	req.tv_nsec = 1000000000L;
	EXPECT_ERR(S4(SYS_clock_nanosleep, LINUX_CLOCK_MONOTONIC, 0, &req, 0),
		   EINVAL);
	req.tv_sec = -1;
	req.tv_nsec = 0;
	EXPECT_ERR(S4(SYS_clock_nanosleep, LINUX_CLOCK_MONOTONIC, 0, &req, 0),
		   EINVAL);
	ts_set_ms(&req, 1);
	EXPECT_ERR(S4(SYS_clock_nanosleep, 100, 0, &req, 0), EINVAL);
	EXPECT_ERR(S4(SYS_clock_nanosleep, LINUX_CLOCK_MONOTONIC_RAW, 0, &req,
		      0), EOPNOTSUPP);
	EXPECT_ERR(S4(SYS_clock_nanosleep, LINUX_CLOCK_THREAD_CPUTIME_ID, 0,
		      &req, 0), EOPNOTSUPP);
	EXPECT_ERR(S4(SYS_clock_nanosleep, LINUX_CLOCK_MONOTONIC, 0, 0, 0),
		   EFAULT);
	return CONF_PASS;
}

/* Fork a child that sends SIGUSR1 to the caller after @ms. */
static long signal_me_after(long ms)
{
	long parent = S0(SYS_getpid);
	long pid = rt_fork();

	if (pid == 0)
	{
		rt_sleep_ms(ms);
		(void)S2(SYS_kill, parent, SIGUSR1);
		rt_exit(0);
	}
	return pid;
}

/*
 * A caught signal ends the sleep with EINTR. A relative sleep stores the
 * unslept time in rem; an absolute one leaves rem alone.
 */
int sleep_clock_nanosleep_eintr_rem(void)
{
	struct timespec req;
	struct timespec rem;
	int status = 0;
	long pid;
	long target;

	EXPECT(rt_sigaction(SIGUSR1, on_signal, 0, 0), 0);

	ts_set_ms(&req, 5000);
	rem.tv_sec = -7;
	rem.tv_nsec = 0;
	OK(pid, signal_me_after(50));
	EXPECT_ERR(S4(SYS_clock_nanosleep, LINUX_CLOCK_MONOTONIC, 0, &req,
		      &rem), EINTR);
	EXPECT(conf_child_status(pid, &status, 5000), pid);
	EXPECT(g_hits, 1);
	CHECK(rem.tv_sec >= 1 && rem.tv_sec <= 5);
	CHECK(rem.tv_nsec >= 0 && rem.tv_nsec < 1000000000L);

	target = clock_ms(LINUX_CLOCK_MONOTONIC) + 5000;
	ts_set_ms(&req, target);
	rem.tv_sec = -7;
	OK(pid, signal_me_after(50));
	EXPECT_ERR(S4(SYS_clock_nanosleep, LINUX_CLOCK_MONOTONIC,
		      LINUX_TIMER_ABSTIME, &req, &rem), EINTR);
	EXPECT(conf_child_status(pid, &status, 5000), pid);
	EXPECT(g_hits, 2);
	EXPECT(rem.tv_sec, -7);
	return CONF_PASS;
}

/* sched_yield(2) always succeeds. */
int sleep_sched_yield_returns_zero(void)
{
	int i;

	for (i = 0; i < 4; i++)
		EXPECT(S0(SYS_sched_yield), 0);
	return CONF_PASS;
}
