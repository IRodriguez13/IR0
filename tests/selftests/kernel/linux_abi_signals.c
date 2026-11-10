/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: linux_abi_signals.c
 * Description: RED selftest (fails on dev 629036ef, not wired into any gate):
 *              signal and wait semantics that diverge from Linux, found by
 *              the conformance harness and the syscall fuzzer
 *              (Documentation/conformance.md). One case per bug; each passes
 *              on Linux. Run by make ir0-selftest-red-signals-{x86,arm64}.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "conf.h"

#include <time.h>

static volatile int g_hits;
static volatile int g_sig;

static void on_signal(int sig)
{
	g_sig = sig;
	g_hits++;
}

/* kernel/signal.c sigprocmask(): SIGKILL and SIGSTOP are never blocked. */
int sig_kill_stop_not_blockable(void)
{
	unsigned long set = SIGBIT(SIGKILL) | SIGBIT(SIGSTOP);
	unsigned long cur = 0;

	EXPECT(S4(SYS_rt_sigprocmask, SIG_BLOCK, &set, 0, 8), 0);
	EXPECT(S4(SYS_rt_sigprocmask, SIG_BLOCK, 0, &cur, 8), 0);
	EXPECT(cur & set, 0);
	return CONF_PASS;
}

/* sigaction(2) SA_RESETHAND: the disposition is SIG_DFL after delivery. */
int sig_sa_resethand_resets(void)
{
	struct ksigaction old;

	EXPECT(rt_sigaction(SIGUSR1, on_signal, SA_RESETHAND, 0), 0);
	EXPECT(S2(SYS_kill, S0(SYS_getpid), SIGUSR1), 0);
	EXPECT(g_hits, 1);
	EXPECT(S4(SYS_rt_sigaction, SIGUSR1, 0, &old, 8), 0);
	CHECK(old.handler == SIG_DFL);
	return CONF_PASS;
}

/* sigsuspend(2) always returns -1/EINTR after a handler ran. */
int sig_sigsuspend_returns_eintr(void)
{
	unsigned long set = SIGBIT(SIGUSR1);
	unsigned long none = 0;

	EXPECT(rt_sigaction(SIGUSR1, on_signal, 0, 0), 0);
	EXPECT(S4(SYS_rt_sigprocmask, SIG_BLOCK, &set, 0, 8), 0);
	EXPECT(S2(SYS_kill, S0(SYS_getpid), SIGUSR1), 0);
	EXPECT_ERR(S2(SYS_rt_sigsuspend, &none, 8), EINTR);
	EXPECT(g_hits, 1);
	return CONF_PASS;
}

/* nanosleep(2): a caught signal sent by another process ends it with EINTR. */
int sig_nanosleep_eintr_from_other_process(void)
{
	struct timespec req = { 3, 0 };
	struct timespec rem = { 0, 0 };
	long parent = S0(SYS_getpid);
	int status = 0;
	long pid;

	EXPECT(rt_sigaction(SIGUSR1, on_signal, 0, 0), 0);
	pid = rt_fork();
	CHECK(pid >= 0);
	if (pid == 0)
	{
		rt_sleep_ms(100);
		rt_exit(S2(SYS_kill, parent, SIGUSR1) == 0 ? 0 : 1);
	}
	EXPECT_ERR(S2(SYS_nanosleep, &req, &rem), EINTR);
	EXPECT(g_hits, 1);
	EXPECT(conf_child_status(pid, &status, 5000), pid);
	return CONF_PASS;
}

/* kernel/exit.c kernel_wait4(): unknown option bits are EINVAL first. */
int sig_wait4_bad_options_einval(void)
{
	int status;

	EXPECT_ERR(rt_wait(-1, &status, 0x7fff0000), EINVAL);
	return CONF_PASS;
}

/*
 * kill(2) with pid -1 signals every process except init and the caller.
 * Guest-only: on a host it would signal the whole session.
 */
int sig_kill_all_spares_caller(void)
{
	int status = 0;
	long victim;

	if (!conf_guest)
		return CONF_TCONF;
	EXPECT(rt_sigaction(SIGUSR1, on_signal, 0, 0), 0);
	victim = rt_fork();
	CHECK(victim >= 0);
	if (victim == 0)
	{
		for (;;)
			rt_sleep_ms(1000);
	}
	rt_sleep_ms(50);
	EXPECT(S2(SYS_kill, -1, SIGUSR1), 0);
	rt_sleep_ms(50);
	EXPECT(g_hits, 0);
	EXPECT(conf_child_status(victim, &status, 2000), victim);
	CHECK(RT_SIGNALED(status) && RT_TERMSIG(status) == SIGUSR1);
	return CONF_PASS;
}

/* signal(7): the handler's first argument is the signal number. */
int sig_handler_gets_signum(void)
{
	EXPECT(rt_sigaction(SIGUSR2, on_signal, 0, 0), 0);
	EXPECT(S2(SYS_kill, S0(SYS_getpid), SIGUSR2), 0);
	EXPECT(g_hits, 1);
	EXPECT(g_sig, SIGUSR2);
	return CONF_PASS;
}
