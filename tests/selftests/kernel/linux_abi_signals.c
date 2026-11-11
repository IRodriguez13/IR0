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

#include <sys/socket.h>
#include <time.h>

extern void rt_restore(void);

static volatile int g_hits;
static volatile int g_sig;
static volatile int g_usr2_hits;

static void on_signal(int sig)
{
	g_sig = sig;
	g_hits++;
}

static void on_usr2(int sig)
{
	(void)sig;
	g_usr2_hits++;
}

/* Raises SIGUSR2 while sa_mask keeps it blocked until rt_sigreturn. */
static void on_usr1_raise_usr2(int sig)
{
	(void)sig;
	g_hits++;
	(void)S2(SYS_kill, S0(SYS_getpid), SIGUSR2);
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
		/* fork(2) inherits the handler: the victim must die by default. */
		(void)rt_sigaction(SIGUSR1, SIG_DFL, 0, 0);
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

/*
 * signal(7) SA_RESTART: a read(2) on a stream socket interrupted by a
 * handler is restarted (unix_stream_read_generic(): -ERESTARTSYS).
 */
int sig_socket_read_restarts(void)
{
	long parent = S0(SYS_getpid);
	int sv[2];
	int status = 0;
	char c = 0;
	long pid;

	EXPECT(rt_sigaction(SIGUSR1, on_signal, SA_RESTART, 0), 0);
	EXPECT(S4(SYS_socketpair, AF_UNIX, SOCK_STREAM, 0, sv), 0);
	pid = rt_fork();
	CHECK(pid >= 0);
	if (pid == 0)
	{
		rt_sleep_ms(100);
		if (S2(SYS_kill, parent, SIGUSR1) != 0)
			rt_exit(1);
		rt_sleep_ms(100);
		rt_exit(S3(SYS_write, sv[1], "x", 1) == 1 ? 0 : 2);
	}
	EXPECT(S3(SYS_read, sv[0], &c, 1), 1);
	EXPECT(c, 'x');
	EXPECT(g_hits, 1);
	EXPECT(conf_child_status(pid, &status, 5000), pid);
	return CONF_PASS;
}

/*
 * A signal that becomes deliverable when rt_sigreturn restores the mask
 * runs on rt_sigreturn's way out (Linux exit_to_user_mode()), before the
 * interrupted code executes another instruction.
 */
int sig_sigreturn_delivers_unmasked(void)
{
	struct ksigaction act;
	long ret;
	int seen;

	EXPECT(rt_sigaction(SIGUSR2, on_usr2, 0, 0), 0);
	act.handler = on_usr1_raise_usr2;
	act.flags = SA_RESTORER;
	act.restorer = rt_restore;
	act.mask = SIGBIT(SIGUSR2);
	EXPECT(S4(SYS_rt_sigaction, SIGUSR1, &act, 0, sizeof(act.mask)), 0);
	ret = S2(SYS_kill, S0(SYS_getpid), SIGUSR1);
	seen = g_usr2_hits;
	EXPECT(ret, 0);
	EXPECT(g_hits, 1);
	EXPECT(seen, 1);
	return CONF_PASS;
}

/*
 * signal(7): a fatal signal ends a task that never enters the kernel. The
 * child spins in userspace; only the timer interrupt returns to it.
 * On a kernel without interrupt-return preemption the spinning child
 * keeps the only CPU and the guest stops.
 */
int sig_kill_cpu_bound_child(void)
{
	static volatile unsigned long spin;
	int status = 0;
	long pid;

	pid = rt_fork();
	CHECK(pid >= 0);
	if (pid == 0)
	{
		for (;;)
			spin++;
	}
	rt_sleep_ms(100);
	EXPECT(S2(SYS_kill, pid, SIGTERM), 0);
	EXPECT(conf_child_status(pid, &status, 3000), pid);
	CHECK(RT_SIGNALED(status) && RT_TERMSIG(status) == SIGTERM);
	return CONF_PASS;
}
