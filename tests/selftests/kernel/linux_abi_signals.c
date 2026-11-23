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

#include <sched.h>
#include <ucontext.h>
#include <sys/socket.h>
#include <time.h>

extern void rt_restore(void);

static volatile int g_hits;
static volatile int g_sig;
static volatile int g_usr2_hits;
static volatile int g_nested_seen;

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

/* Raises an unblocked SIGUSR2 and records whether its handler already ran. */
static void on_usr1_nest_usr2(int sig)
{
	(void)sig;
	g_hits++;
	(void)S2(SYS_kill, S0(SYS_getpid), SIGUSR2);
	g_nested_seen = g_usr2_hits;
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
 * signal(7): the handler's first argument is the signal number also when
 * the signal interrupts code that never enters the kernel. The child
 * spins in userspace when the parent signals it, so only the interrupt
 * return to user mode delivers it; the child exits with the number its
 * handler saw. On arm64 the first argument and the return value share
 * x0: the interrupted x0 must not overwrite it.
 */
int sig_async_handler_gets_signum(void)
{
	static volatile unsigned long spin;
	int status = 0;
	long pid;

	EXPECT(rt_sigaction(SIGUSR1, on_signal, 0, 0), 0);
	pid = rt_fork();
	CHECK(pid >= 0);
	if (pid == 0)
	{
		while (!g_hits)
			spin++;
		rt_exit(g_sig);
	}
	rt_sleep_ms(100);
	EXPECT(S2(SYS_kill, pid, SIGUSR1), 0);
	EXPECT(conf_child_status(pid, &status, 3000), pid);
	CHECK(RT_EXITED(status));
	EXPECT(RT_EXITCODE(status), SIGUSR1);
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
 * signal(7): every signal without a handler whose default action is not
 * Ign or Stop terminates the task, SIGPROF and SIGSYS included.
 */
int sig_default_action_terminates(void)
{
	static const int sigs[] = { SIGPROF, SIGSYS, SIGXCPU, SIGVTALRM };
	unsigned int i;

	for (i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++)
	{
		int status = 0;
		long pid = rt_fork();

		CHECK(pid >= 0);
		if (pid == 0)
		{
			(void)S2(SYS_kill, S0(SYS_getpid), sigs[i]);
			rt_exit(0);
		}
		EXPECT(conf_child_status(pid, &status, 2000), pid);
		CHECK(RT_SIGNALED(status) && RT_TERMSIG(status) == sigs[i]);
	}
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

/*
 * signal(7): handlers nest. A signal that is not blocked while a handler
 * runs is delivered on that handler's next return to user mode
 * (exit_to_user_mode_loop()), inside the outer handler, not after its
 * rt_sigreturn.
 */
int sig_nested_handler_runs(void)
{
	EXPECT(rt_sigaction(SIGUSR2, on_usr2, 0, 0), 0);
	EXPECT(rt_sigaction(SIGUSR1, on_usr1_nest_usr2, 0, 0), 0);
	EXPECT(S2(SYS_kill, S0(SYS_getpid), SIGUSR1), 0);
	EXPECT(g_hits, 1);
	EXPECT(g_nested_seen, 1);
	EXPECT(g_usr2_hits, 1);
	return CONF_PASS;
}

/*
 * kernel/signal.c do_sigpending(): rt_sigpending reports the blocked
 * signals that wait for the caller. A set larger than the kernel's is
 * EINVAL.
 */
int sig_rt_sigpending_reports_blocked(void)
{
	unsigned long set = SIGBIT(SIGUSR1);
	unsigned long pending = 0;

	EXPECT(rt_sigaction(SIGUSR1, on_signal, 0, 0), 0);
	EXPECT(S4(SYS_rt_sigprocmask, SIG_BLOCK, &set, 0, 8), 0);
	EXPECT(S2(SYS_rt_sigpending, &pending, 8), 0);
	EXPECT(pending, 0);
	EXPECT(S2(SYS_kill, S0(SYS_getpid), SIGUSR1), 0);
	EXPECT(S2(SYS_rt_sigpending, &pending, 8), 0);
	EXPECT(pending, SIGBIT(SIGUSR1));
	EXPECT_ERR(S2(SYS_rt_sigpending, &pending, 16), EINVAL);
	EXPECT_ERR(S2(SYS_rt_sigpending, 8, 8), EFAULT);
	EXPECT(g_hits, 0);
	EXPECT(S4(SYS_rt_sigprocmask, SIG_UNBLOCK, &set, 0, 8), 0);
	EXPECT(g_hits, 1);
	return CONF_PASS;
}

/*
 * include/uapi/asm-generic/signal.h: _NSIG is 64 and valid_signal()
 * accepts 1.._NSIG, so signal 64 (SIGRTMAX) is a real signal held in bit
 * 63 of the set.
 */
int sig_rtmax_is_valid(void)
{
	unsigned long set = SIGBIT(64);
	unsigned long pending = 0;

	EXPECT(rt_sigaction(64, on_signal, 0, 0), 0);
	EXPECT_ERR(rt_sigaction(65, on_signal, 0, 0), EINVAL);
	EXPECT(S4(SYS_rt_sigprocmask, SIG_BLOCK, &set, 0, 8), 0);
	EXPECT(S2(SYS_kill, S0(SYS_getpid), 64), 0);
	EXPECT(S2(SYS_rt_sigpending, &pending, 8), 0);
	EXPECT(pending, SIGBIT(64));
	EXPECT(S4(SYS_rt_sigprocmask, SIG_UNBLOCK, &set, 0, 8), 0);
	EXPECT(g_hits, 1);
	EXPECT(g_sig, 64);
	return CONF_PASS;
}

static volatile unsigned long g_uc_mask;
static volatile int g_uc_edit;

/* Records uc_sigmask word 0 and, when asked, blocks SIGUSR2 through it. */
static void on_ucontext(int sig, void *si, void *uc)
{
	unsigned long *word = (unsigned long *)&((ucontext_t *)uc)->uc_sigmask;

	(void)si;
	g_uc_mask = word[0];
	if (g_uc_edit)
		word[0] |= SIGBIT(SIGUSR2);
	g_sig = sig;
	g_hits++;
}

static long set_ucontext_handler(int sig)
{
	struct ksigaction act;

	act.handler = (void (*)(int))(uintptr_t)on_ucontext;
	act.flags = SA_SIGINFO | SA_RESTORER;
	act.restorer = rt_restore;
	act.mask = 0;
	return S4(SYS_rt_sigaction, sig, &act, 0, sizeof(act.mask));
}

/*
 * arch/x86/kernel/signal.c setup_rt_frame(): uc_sigmask is the mask before
 * delivery, in the sigset_t layout (signal N in bit N-1).
 */
int sig_ucontext_sigmask_layout(void)
{
	unsigned long set = SIGBIT(SIGUSR2) | SIGBIT(64);

	EXPECT(set_ucontext_handler(SIGUSR1), 0);
	EXPECT(S4(SYS_rt_sigprocmask, SIG_BLOCK, &set, 0, 8), 0);
	EXPECT(S2(SYS_kill, S0(SYS_getpid), SIGUSR1), 0);
	EXPECT(g_hits, 1);
	EXPECT(g_uc_mask, set);
	return CONF_PASS;
}

/*
 * kernel/signal.c restore_sigcontext() / rt_sigreturn: the mask after the
 * handler is the uc_sigmask the handler leaves in its ucontext.
 */
int sig_sigreturn_restores_uc_sigmask(void)
{
	unsigned long cur = 0;

	g_uc_edit = 1;
	EXPECT(set_ucontext_handler(SIGUSR1), 0);
	EXPECT(S2(SYS_kill, S0(SYS_getpid), SIGUSR1), 0);
	EXPECT(g_hits, 1);
	EXPECT(g_uc_mask, 0);
	EXPECT(S4(SYS_rt_sigprocmask, SIG_BLOCK, 0, &cur, 8), 0);
	EXPECT(cur, SIGBIT(SIGUSR2));
	return CONF_PASS;
}

static unsigned char g_tstack[65536] __attribute__((aligned(16)));

static long g_tstack_top(void)
{
	return (long)(uintptr_t)(g_tstack + sizeof(g_tstack));
}

/*
 * kernel/fork.c copy_process(): CLONE_THREAD needs CLONE_SIGHAND, which
 * needs CLONE_VM.
 */
int sig_clone_thread_needs_sighand(void)
{
	EXPECT_ERR(S5(SYS_clone, CLONE_VM | CLONE_THREAD, g_tstack_top(), 0, 0,
		      0), EINVAL);
	EXPECT_ERR(S5(SYS_clone, CLONE_THREAD | CLONE_SIGHAND,
		      g_tstack_top(), 0, 0, 0), EINVAL);
	return CONF_PASS;
}

/* Linux siginfo_t prefix: _kill, _rt and the TKILL form share pid and uid. */
struct sig_info
{
	int signo;
	int err;
	int code;
	int pad;
	int pid;
	unsigned int uid;
	long value;
	char rest[128 - 32];
};

#define SIG_SI_USER 0
#define SIG_SI_QUEUE (-1)
#define SIG_SI_TKILL (-6)
#define SIG_LOG_MAX 8
#define FAR_TID 0x3ffffff0L

static volatile int g_log_n;
static volatile int g_log_sig[SIG_LOG_MAX];
static volatile int g_log_code[SIG_LOG_MAX];
static volatile int g_log_pid[SIG_LOG_MAX];
static volatile unsigned int g_log_uid[SIG_LOG_MAX];
static volatile long g_log_value[SIG_LOG_MAX];
static volatile long g_log_tid[SIG_LOG_MAX];
static volatile unsigned long g_log_sp[SIG_LOG_MAX];

static void on_info(int sig, struct sig_info *si, void *uc)
{
	volatile int here = 0;
	int n = g_log_n;

	(void)uc;
	if (n >= SIG_LOG_MAX)
		return;
	g_log_sig[n] = sig;
	g_log_code[n] = si->code;
	g_log_pid[n] = si->pid;
	g_log_uid[n] = si->uid;
	g_log_value[n] = si->value;
	g_log_tid[n] = S0(SYS_gettid);
	g_log_sp[n] = (unsigned long)&here;
	g_log_n = n + 1;
}

/* rt_sigaction with an SA_SIGINFO handler and @mask (Linux bit sig-1). */
static long set_info_handler(int sig, unsigned long mask)
{
	struct ksigaction act;

	act.handler = (void (*)(int))(uintptr_t)on_info;
	act.flags = SA_SIGINFO | SA_RESTORER;
	act.restorer = rt_restore;
	act.mask = mask;
	return S4(SYS_rt_sigaction, sig, &act, 0, sizeof(act.mask));
}

static long queue_info(long pid, int sig, int code, long value)
{
	struct sig_info si;

	rt_fill(&si, 0, sizeof(si));
	si.signo = sig;
	si.code = code;
	si.pid = (int)S0(SYS_getpid);
	si.uid = (unsigned int)S0(SYS_getuid);
	si.value = value;
	return S3(SYS_rt_sigqueueinfo, pid, sig, &si);
}

/*
 * signal(7): real-time signals queue (one delivery per send, each with its
 * own siginfo), and the lowest-numbered pending signal is delivered first.
 */
int sig_rt_queue_lowest_first(void)
{
	unsigned long rt = SIGBIT(35) | SIGBIT(40);
	int i;

	EXPECT(set_info_handler(35, rt), 0);
	EXPECT(set_info_handler(40, rt), 0);
	EXPECT(S4(SYS_rt_sigprocmask, SIG_BLOCK, &rt, 0, 8), 0);
	for (i = 1; i <= 3; i++)
		EXPECT(queue_info(S0(SYS_getpid), 40, SIG_SI_QUEUE, i), 0);
	EXPECT(queue_info(S0(SYS_getpid), 35, SIG_SI_QUEUE, 7), 0);
	EXPECT(g_log_n, 0);
	EXPECT(S4(SYS_rt_sigprocmask, SIG_UNBLOCK, &rt, 0, 8), 0);
	EXPECT(g_log_n, 4);
	EXPECT(g_log_sig[0], 35);
	EXPECT(g_log_value[0], 7);
	for (i = 1; i <= 3; i++)
	{
		EXPECT(g_log_sig[i], 40);
		EXPECT(g_log_value[i], i);
		EXPECT(g_log_code[i], SIG_SI_QUEUE);
	}
	return CONF_PASS;
}

/*
 * kernel/signal.c: kill() reports SI_USER, tkill()/tgkill() SI_TKILL and
 * rt_sigqueueinfo() the caller's SI_QUEUE info, each with the sender's pid
 * and uid.
 */
int sig_siginfo_names_sender(void)
{
	long pid = S0(SYS_getpid);
	long uid = S0(SYS_getuid);

	EXPECT(set_info_handler(SIGUSR1, 0), 0);
	EXPECT(set_info_handler(SIGUSR2, 0), 0);
	EXPECT(S2(SYS_kill, pid, SIGUSR1), 0);
	EXPECT(S2(SYS_tkill, S0(SYS_gettid), SIGUSR2), 0);
	EXPECT(S3(SYS_tgkill, pid, S0(SYS_gettid), SIGUSR2), 0);
	EXPECT(queue_info(pid, SIGUSR1, SIG_SI_QUEUE, 42), 0);
	EXPECT(g_log_n, 4);
	EXPECT(g_log_sig[0], SIGUSR1);
	EXPECT(g_log_code[0], SIG_SI_USER);
	EXPECT(g_log_pid[0], pid);
	EXPECT(g_log_uid[0], uid);
	EXPECT(g_log_code[1], SIG_SI_TKILL);
	EXPECT(g_log_pid[1], pid);
	EXPECT(g_log_code[2], SIG_SI_TKILL);
	EXPECT(g_log_code[3], SIG_SI_QUEUE);
	EXPECT(g_log_value[3], 42);
	return CONF_PASS;
}

/*
 * kernel/signal.c do_rt_sigqueueinfo(): another process may not forge a
 * kernel or kill() si_code (>= 0) or SI_TKILL: EPERM. The caller may
 * queue any code to itself. rt_tgsigqueueinfo names the thread.
 */
int sig_sigqueueinfo_rules(void)
{
	struct sig_info si;
	int status = 0;
	long pid;

	EXPECT(set_info_handler(SIGUSR1, 0), 0);
	OK(pid, rt_fork());
	if (pid == 0)
	{
		for (;;)
			rt_sleep_ms(1000);
	}
	EXPECT_ERR(queue_info(pid, SIGUSR1, SIG_SI_USER, 0), EPERM);
	EXPECT_ERR(queue_info(pid, SIGUSR1, SIG_SI_TKILL, 0), EPERM);
	EXPECT_ERR(queue_info(pid, 65, SIG_SI_QUEUE, 0), EINVAL);
	EXPECT(S2(SYS_kill, pid, SIGKILL), 0);
	EXPECT(conf_child_status(pid, &status, 3000), pid);
	EXPECT(queue_info(S0(SYS_getpid), SIGUSR1, SIG_SI_USER, 5), 0);
	EXPECT(g_log_n, 1);
	EXPECT(g_log_code[0], SIG_SI_USER);
	rt_fill(&si, 0, sizeof(si));
	si.signo = SIGUSR1;
	si.code = SIG_SI_QUEUE;
	si.value = 9;
	EXPECT(S4(SYS_rt_tgsigqueueinfo, S0(SYS_getpid), S0(SYS_gettid),
		  SIGUSR1, &si), 0);
	EXPECT(g_log_n, 2);
	EXPECT(g_log_value[1], 9);
	EXPECT_ERR(S4(SYS_rt_tgsigqueueinfo, S0(SYS_getpid), FAR_TID,
		      SIGUSR1, &si), ESRCH);
	return CONF_PASS;
}
