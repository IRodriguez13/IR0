/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: cases_proc.c
 * Description: Conformance cases for pipes, fork/exec/wait and signals.
 *              Expected results follow Linux (fs/pipe.c, kernel/fork.c,
 *              kernel/exit.c, fs/exec.c, kernel/signal.c) and signal(7).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "conf.h"

#include <sys/utsname.h>
#include <time.h>

#define WAIT_MS 5000L
#define FAR_PID 0x3ffffff0L

static volatile int g_hits;
static volatile int g_last_sig;
static volatile unsigned long g_mask_in_handler;

static void on_signal(int sig)
{
	g_hits++;
	g_last_sig = sig;
}

static void on_signal_mask(int sig)
{
	unsigned long cur = 0;

	(void)S4(SYS_rt_sigprocmask, SIG_BLOCK, 0, &cur, sizeof(cur));
	g_mask_in_handler = cur;
	on_signal(sig);
}

/* Fork a child that exits with code; return its pid. */
static long spawn_exit(int code)
{
	long pid = rt_fork();

	if (pid == 0)
		rt_exit(code);
	return pid;
}

int pipe_roundtrip(void)
{
	char buf[16];
	int p[2];
	long r;

	OK(r, S2(SYS_pipe2, p, 0));
	EXPECT(r, 0);
	CHECK(p[0] >= 0 && p[1] >= 0 && p[0] != p[1]);
	EXPECT(S3(SYS_write, p[1], "pipe!", 5), 5);
	EXPECT(S3(SYS_read, p[0], buf, sizeof(buf)), 5);
	CHECK(rt_memeq(buf, "pipe!", 5));
	EXPECT_ERR(S3(SYS_write, p[0], "x", 1), EBADF);
	EXPECT_ERR(S3(SYS_read, p[1], buf, 1), EBADF);
	return CONF_PASS;
}

int pipe_eof_after_writer_close(void)
{
	char buf[4];
	int p[2];
	long r;

	OK(r, S2(SYS_pipe2, p, 0));
	EXPECT(S3(SYS_write, p[1], "ab", 2), 2);
	EXPECT(S1(SYS_close, p[1]), 0);
	EXPECT(S3(SYS_read, p[0], buf, sizeof(buf)), 2);
	EXPECT(S3(SYS_read, p[0], buf, sizeof(buf)), 0);
	return CONF_PASS;
}

int pipe_epipe_when_ignored(void)
{
	int p[2];
	long r;

	OK(r, rt_sigaction(SIGPIPE, SIG_IGN, 0, 0));
	OK(r, S2(SYS_pipe2, p, 0));
	EXPECT(S1(SYS_close, p[0]), 0);
	EXPECT_ERR(S3(SYS_write, p[1], "x", 1), EPIPE);
	return CONF_PASS;
}

int pipe_sigpipe_kills_writer(void)
{
	int status = 0;
	int p[2];
	long r;
	long pid;

	OK(r, S2(SYS_pipe2, p, 0));
	EXPECT(S1(SYS_close, p[0]), 0);
	OK(pid, rt_fork());
	if (pid == 0)
	{
		(void)S3(SYS_write, p[1], "x", 1);
		rt_exit(0);
	}
	EXPECT(conf_child_status(pid, &status, WAIT_MS), pid);
	CHECK(RT_SIGNALED(status));
	EXPECT(RT_TERMSIG(status), SIGPIPE);
	return CONF_PASS;
}

int pipe_nonblock_eagain(void)
{
	char buf[4];
	int p[2];
	long r;
	long fl;

	OK(r, S2(SYS_pipe2, p, O_NONBLOCK | O_CLOEXEC));
	EXPECT_ERR(S3(SYS_read, p[0], buf, sizeof(buf)), EAGAIN);
	OK(fl, S2(SYS_fcntl, p[0], F_GETFL));
	CHECK(fl & O_NONBLOCK);
	EXPECT(S2(SYS_fcntl, p[1], F_GETFD), FD_CLOEXEC);
	EXPECT_ERR(S2(SYS_pipe2, p, 0x7fff0000), EINVAL);
	EXPECT_ERR(S2(SYS_pipe2, 0, 0), EFAULT);
	return CONF_PASS;
}

int pipe_capacity_then_eagain(void)
{
	static char chunk[1024];
	long total = 0;
	long w = 0;
	int p[2];
	long r;
	int i;

	OK(r, S2(SYS_pipe2, p, O_NONBLOCK));
	for (i = 0; i < 1024; i++)
	{
		w = S3(SYS_write, p[1], chunk, sizeof(chunk));
		if (w < 0)
			break;
		total += w;
	}
	EXPECT(w, -EAGAIN);
	CHECK(total >= 4096);
	rt_diag("pipe_capacity", total);
	return CONF_PASS;
}

int proc_fork_exit_status(void)
{
	int status = 0;
	long pid;

	OK(pid, spawn_exit(42));
	CHECK(pid > 0);
	EXPECT(conf_child_status(pid, &status, WAIT_MS), pid);
	CHECK(RT_EXITED(status));
	EXPECT(RT_EXITCODE(status), 42);
	OK(pid, spawn_exit(256 + 3));
	EXPECT(conf_child_status(pid, &status, WAIT_MS), pid);
	EXPECT(RT_EXITCODE(status), 3);
	return CONF_PASS;
}

int proc_wait_echild(void)
{
	int status;

	EXPECT_ERR(rt_wait(-1, &status, 0), ECHILD);
	EXPECT_ERR(rt_wait(FAR_PID, &status, 0), ECHILD);
	EXPECT_ERR(rt_wait(-1, &status, 0x7fff0000), EINVAL);
	return CONF_PASS;
}

int proc_wait_wnohang(void)
{
	int status = 0;
	long pid;

	OK(pid, rt_fork());
	if (pid == 0)
	{
		rt_sleep_ms(200);
		rt_exit(5);
	}
	EXPECT(rt_wait(pid, &status, WNOHANG), 0);
	EXPECT(conf_child_status(pid, &status, WAIT_MS), pid);
	EXPECT(RT_EXITCODE(status), 5);
	return CONF_PASS;
}

int proc_wait_specific_pid(void)
{
	int status = 0;
	long a;
	long b;

	OK(a, spawn_exit(11));
	OK(b, spawn_exit(22));
	EXPECT(conf_child_status(b, &status, WAIT_MS), b);
	EXPECT(RT_EXITCODE(status), 22);
	EXPECT(conf_child_status(a, &status, WAIT_MS), a);
	EXPECT(RT_EXITCODE(status), 11);
	EXPECT_ERR(rt_wait(a, &status, WNOHANG), ECHILD);
	return CONF_PASS;
}

int proc_wait_null_status(void)
{
	long pid;

	OK(pid, spawn_exit(0));
	rt_sleep_ms(50);
	EXPECT(rt_wait(pid, 0, 0), pid);
	return CONF_PASS;
}

int proc_ids(void)
{
	int status = 0;
	long me;
	long pid;

	OK(me, S0(SYS_getpid));
	EXPECT(S0(SYS_gettid), me);
	OK(pid, rt_fork());
	if (pid == 0)
		rt_exit(S0(SYS_getppid) == me ? 0 : 1);
	EXPECT(conf_child_status(pid, &status, WAIT_MS), pid);
	EXPECT(status, 0);
	return CONF_PASS;
}

int proc_setsid(void)
{
	int status = 0;
	long pid;

	OK(pid, rt_fork());
	if (pid == 0)
	{
		long me = S0(SYS_getpid);

		if (S0(SYS_setsid) != me)
			rt_exit(1);
		if (S1(SYS_getpgid, 0) != me || S1(SYS_getsid, 0) != me)
			rt_exit(2);
		/* A session leader cannot create another session. */
		rt_exit(S0(SYS_setsid) == -EPERM ? 0 : 3);
	}
	EXPECT(conf_child_status(pid, &status, WAIT_MS), pid);
	EXPECT(RT_EXITCODE(status), 0);
	return CONF_PASS;
}

int proc_uname(void)
{
	struct utsname u;

	EXPECT(S1(SYS_uname, &u), 0);
	CHECK(u.sysname[0] != '\0' && u.release[0] != '\0');
	CHECK(rt_streq(u.machine, RT_MACHINE));
	EXPECT_ERR(S1(SYS_uname, 0), EFAULT);
	return CONF_PASS;
}

int proc_exec_errors(void)
{
	char *argv[2];
	long fd;

	argv[0] = (char *)"x";
	argv[1] = 0;
	EXPECT_ERR(S3(SYS_execve, "/nonexistent/prog", argv, 0), ENOENT);
	OK(fd, conf_creat("noexec", "#!/bin/sh\n"));
	EXPECT(S1(SYS_close, fd), 0);
	EXPECT_ERR(S3(SYS_execve, "noexec", argv, 0), EACCES);
	EXPECT_ERR(S3(SYS_execve, ".", argv, 0), EACCES);
	EXPECT_ERR(S3(SYS_execve, 0, argv, 0), EFAULT);
	return CONF_PASS;
}

static long exec_probe(const char *mode, long arg, const char *extra)
{
	char num[24];
	char *argv[6];
	int status = 0;
	long pid;
	long ret;

	num[0] = '\0';
	rt_catn(num, sizeof(num), arg);
	argv[0] = (char *)conf_self;
	argv[1] = (char *)"--probe";
	argv[2] = (char *)mode;
	argv[3] = num;
	argv[4] = (char *)extra;
	argv[5] = 0;
	pid = rt_fork();
	if (pid < 0)
		return pid;
	if (pid == 0)
	{
		ret = S3(SYS_execve, conf_self, argv, 0);
		rt_exit(ret == -ENOSYS ? 120 : 121);
	}
	ret = conf_child_status(pid, &status, WAIT_MS);
	if (ret < 0)
		return ret;
	if (!RT_EXITED(status))
		return -1000 - RT_TERMSIG(status);
	return RT_EXITCODE(status);
}

int proc_exec_exit_status(void)
{
	long r = exec_probe("exit", 7, 0);

	if (r == 120)
		return CONF_TCONF;
	EXPECT(r, 7);
	EXPECT(exec_probe("argv", 0, "last"), 0);
	return CONF_PASS;
}

int proc_exec_cloexec(void)
{
	long keep;
	long drop;

	OK(keep, conf_creat("keep", 0));
	OK(drop, S4(SYS_openat, AT_FDCWD, "drop", O_CREAT | O_RDWR | O_CLOEXEC,
		    0644));
	EXPECT(exec_probe("fdopen", keep, 0), 0);
	EXPECT(exec_probe("fdclosed", drop, 0), 0);
	return CONF_PASS;
}

int proc_fork_fd_inherit(void)
{
	char buf[8];
	int status = 0;
	int p[2];
	long pid;
	long r;

	OK(r, S2(SYS_pipe2, p, 0));
	OK(pid, rt_fork());
	if (pid == 0)
		rt_exit(S3(SYS_write, p[1], "kid", 3) == 3 ? 0 : 1);
	EXPECT(conf_child_status(pid, &status, WAIT_MS), pid);
	EXPECT(status, 0);
	EXPECT(S3(SYS_read, p[0], buf, sizeof(buf)), 3);
	CHECK(rt_memeq(buf, "kid", 3));
	return CONF_PASS;
}

int sig_handler_runs(void)
{
	long r;

	OK(r, rt_sigaction(SIGUSR1, on_signal, 0, 0));
	EXPECT(S2(SYS_kill, S0(SYS_getpid), SIGUSR1), 0);
	EXPECT(g_hits, 1);
	EXPECT(g_last_sig, SIGUSR1);
	EXPECT(S2(SYS_kill, S0(SYS_getpid), 0), 0);
	EXPECT(g_hits, 1);
	return CONF_PASS;
}

int sig_old_action(void)
{
	struct ksigaction old;
	long r;

	OK(r, rt_sigaction(SIGUSR2, on_signal, SA_RESTART, 0));
	rt_fill(&old, 0, sizeof(old));
	OK(r, rt_sigaction(SIGUSR2, SIG_IGN, 0, &old));
	CHECK(old.handler == on_signal);
	CHECK(old.flags & SA_RESTART);
	EXPECT(S4(SYS_rt_sigaction, SIGUSR2, 0, &old, 8), 0);
	CHECK(old.handler == SIG_IGN);
	EXPECT_ERR(S4(SYS_rt_sigaction, SIGUSR2, 0, &old, 4), EINVAL);
	return CONF_PASS;
}

int sig_block_pending(void)
{
	unsigned long set = SIGBIT(SIGUSR1);
	unsigned long pending = 0;
	long r;

	OK(r, rt_sigaction(SIGUSR1, on_signal, 0, 0));
	EXPECT(S4(SYS_rt_sigprocmask, SIG_BLOCK, &set, 0, 8), 0);
	EXPECT(S2(SYS_kill, S0(SYS_getpid), SIGUSR1), 0);
	EXPECT(S2(SYS_kill, S0(SYS_getpid), SIGUSR1), 0);
	EXPECT(g_hits, 0);
	EXPECT(S2(SYS_rt_sigpending, &pending, 8), 0);
	CHECK(pending & SIGBIT(SIGUSR1));
	EXPECT(S4(SYS_rt_sigprocmask, SIG_UNBLOCK, &set, 0, 8), 0);
	/* Standard signals do not queue: two sends, one delivery. */
	EXPECT(g_hits, 1);
	EXPECT(S2(SYS_rt_sigpending, &pending, 8), 0);
	CHECK(!(pending & SIGBIT(SIGUSR1)));
	EXPECT_ERR(S4(SYS_rt_sigprocmask, 99, &set, 0, 8), EINVAL);
	return CONF_PASS;
}

int sig_kill_stop_uncatchable(void)
{
	unsigned long set = SIGBIT(SIGKILL) | SIGBIT(SIGSTOP);
	unsigned long cur = 0;

	EXPECT_ERR(rt_sigaction(SIGKILL, on_signal, 0, 0), EINVAL);
	EXPECT_ERR(rt_sigaction(SIGSTOP, SIG_IGN, 0, 0), EINVAL);
	/* Blocking them is silently ignored. */
	EXPECT(S4(SYS_rt_sigprocmask, SIG_BLOCK, &set, 0, 8), 0);
	EXPECT(S4(SYS_rt_sigprocmask, SIG_BLOCK, 0, &cur, 8), 0);
	EXPECT(cur & set, 0);
	return CONF_PASS;
}

int sig_invalid_signum(void)
{
	EXPECT_ERR(rt_sigaction(0, on_signal, 0, 0), EINVAL);
	EXPECT_ERR(rt_sigaction(65, on_signal, 0, 0), EINVAL);
	EXPECT_ERR(S2(SYS_kill, S0(SYS_getpid), 65), EINVAL);
	EXPECT_ERR(S2(SYS_kill, S0(SYS_getpid), -1), EINVAL);
	return CONF_PASS;
}

int sig_kill_esrch(void)
{
	EXPECT_ERR(S2(SYS_kill, FAR_PID, SIGUSR1), ESRCH);
	EXPECT_ERR(S2(SYS_kill, FAR_PID, 0), ESRCH);
	return CONF_PASS;
}

int sig_ignored_is_discarded(void)
{
	unsigned long pending = 0;
	long r;

	OK(r, rt_sigaction(SIGUSR1, SIG_IGN, 0, 0));
	EXPECT(S2(SYS_kill, S0(SYS_getpid), SIGUSR1), 0);
	EXPECT(S2(SYS_rt_sigpending, &pending, 8), 0);
	EXPECT(pending & SIGBIT(SIGUSR1), 0);
	return CONF_PASS;
}

int sig_default_terminates(void)
{
	int status = 0;
	long pid;

	OK(pid, rt_fork());
	if (pid == 0)
	{
		(void)S2(SYS_kill, S0(SYS_getpid), SIGUSR1);
		rt_exit(0);
	}
	EXPECT(conf_child_status(pid, &status, WAIT_MS), pid);
	CHECK(RT_SIGNALED(status));
	EXPECT(RT_TERMSIG(status), SIGUSR1);
	return CONF_PASS;
}

int sig_kill_child_sigterm(void)
{
	int status = 0;
	long pid;

	OK(pid, rt_fork());
	if (pid == 0)
	{
		for (;;)
			rt_sleep_ms(1000);
	}
	rt_sleep_ms(20);
	EXPECT(S2(SYS_kill, pid, SIGTERM), 0);
	EXPECT(conf_child_status(pid, &status, WAIT_MS), pid);
	CHECK(RT_SIGNALED(status));
	EXPECT(RT_TERMSIG(status), SIGTERM);
	return CONF_PASS;
}

int sig_resethand(void)
{
	struct ksigaction old;
	long r;

	OK(r, rt_sigaction(SIGUSR1, on_signal, SA_RESETHAND, 0));
	EXPECT(S2(SYS_kill, S0(SYS_getpid), SIGUSR1), 0);
	EXPECT(g_hits, 1);
	EXPECT(S4(SYS_rt_sigaction, SIGUSR1, 0, &old, 8), 0);
	CHECK(old.handler == SIG_DFL);
	return CONF_PASS;
}

int sig_mask_during_handler(void)
{
	unsigned long cur = 1;
	long r;

	OK(r, rt_sigaction(SIGUSR1, on_signal_mask, 0, 0));
	EXPECT(S2(SYS_kill, S0(SYS_getpid), SIGUSR1), 0);
	EXPECT(g_hits, 1);
	CHECK(g_mask_in_handler & SIGBIT(SIGUSR1));
	EXPECT(S4(SYS_rt_sigprocmask, SIG_BLOCK, 0, &cur, 8), 0);
	EXPECT(cur & SIGBIT(SIGUSR1), 0);
	g_hits = 0;
	OK(r, rt_sigaction(SIGUSR1, on_signal_mask, SA_NODEFER, 0));
	EXPECT(S2(SYS_kill, S0(SYS_getpid), SIGUSR1), 0);
	EXPECT(g_hits, 1);
	EXPECT(g_mask_in_handler & SIGBIT(SIGUSR1), 0);
	return CONF_PASS;
}

int sig_sigsuspend(void)
{
	unsigned long set = SIGBIT(SIGUSR1);
	unsigned long none = 0;
	unsigned long cur = 0;
	long r;

	OK(r, rt_sigaction(SIGUSR1, on_signal, 0, 0));
	EXPECT(S4(SYS_rt_sigprocmask, SIG_BLOCK, &set, 0, 8), 0);
	EXPECT(S2(SYS_kill, S0(SYS_getpid), SIGUSR1), 0);
	EXPECT(g_hits, 0);
	EXPECT_ERR(S2(SYS_rt_sigsuspend, &none, 8), EINTR);
	EXPECT(g_hits, 1);
	EXPECT(S4(SYS_rt_sigprocmask, SIG_BLOCK, 0, &cur, 8), 0);
	CHECK(cur & SIGBIT(SIGUSR1));
	return CONF_PASS;
}

int sig_nanosleep_eintr(void)
{
	struct timespec req = { 5, 0 };
	struct timespec rem = { 0, 0 };
	int status = 0;
	long parent = S0(SYS_getpid);
	long pid;
	long r;

	OK(r, rt_sigaction(SIGUSR1, on_signal, 0, 0));
	OK(pid, rt_fork());
	if (pid == 0)
	{
		rt_sleep_ms(100);
		rt_exit(S2(SYS_kill, parent, SIGUSR1) == 0 ? 0 : 1);
	}
	EXPECT_ERR(S2(SYS_nanosleep, &req, &rem), EINTR);
	EXPECT(g_hits, 1);
	CHECK(rem.tv_sec < 5 && (rem.tv_sec > 0 || rem.tv_nsec > 0));
	EXPECT(conf_child_status(pid, &status, WAIT_MS), pid);
	return CONF_PASS;
}

int sig_sigchld_on_exit(void)
{
	int status = 0;
	long pid;
	long r;

	OK(r, rt_sigaction(SIGCHLD, on_signal, 0, 0));
	OK(pid, spawn_exit(0));
	EXPECT(conf_child_status(pid, &status, WAIT_MS), pid);
	EXPECT(g_hits, 1);
	EXPECT(g_last_sig, SIGCHLD);
	return CONF_PASS;
}
