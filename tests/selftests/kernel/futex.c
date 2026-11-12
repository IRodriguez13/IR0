/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: futex.c
 * Description: futex(2) wait/wake as on Linux: FUTEX_WAIT fails with EAGAIN
 *              when the word changed, FUTEX_WAKE reports how many waiters
 *              it woke, a waiting thread sleeps until a sibling thread
 *              wakes it, and a caught signal ends the wait with EINTR.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <errno.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

/* futex(2), errno(3) and clone(2) values; nolibc has no headers. */
#define LINUX_FUTEX_WAIT 0
#define LINUX_FUTEX_WAKE 1
#define LINUX_EAGAIN 11
#define LINUX_EINTR 4
#define THREAD_FLAGS 0x10f00UL /* CLONE_VM|FS|FILES|SIGHAND|THREAD */
#define DELAY_NS 50000000L
#define THREAD_STACK 16384

static volatile int g_word;
static volatile int g_done;
static volatile long g_thread_wake_ret = -100;
static volatile int g_sigusr1_seen;
static unsigned char g_thread_stack[THREAD_STACK] __attribute__((aligned(16)));

static long futex(volatile int *uaddr, int op, int val)
{
	return syscall(SYS_futex, (long)uaddr, op, val, 0);
}

static void sleep_ns(long ns)
{
	struct timespec ts = { 0, ns };

	nanosleep(&ts, 0);
}

/*
 * Start fn on @stack_top in a thread of this process; the thread exits
 * with SYS_exit when fn returns. Returns the thread id or -errno.
 */
static long thread_start(void (*fn)(void), void *stack_top)
{
#if defined(__x86_64__)
	register long r10 __asm__("r10") = 0;
	register long r8 __asm__("r8") = 0;
	long ret;

	__asm__ volatile("syscall\n\t"
			 "test %%rax, %%rax\n\t"
			 "jnz 1f\n\t"
			 "xor %%ebp, %%ebp\n\t"
			 "call *%[fn]\n\t"
			 "mov $60, %%eax\n\t"
			 "xor %%edi, %%edi\n\t"
			 "syscall\n\t"
			 "1:"
			 : "=a"(ret)
			 : "0"(56L), "D"(THREAD_FLAGS), "S"(stack_top), "d"(0L),
			   "r"(r10), "r"(r8), [fn] "r"(fn)
			 : "rcx", "r11", "memory");
	return ret;
#elif defined(__aarch64__)
	register long x8 __asm__("x8") = 220;
	register long x0 __asm__("x0") = (long)THREAD_FLAGS;
	register long x1 __asm__("x1") = (long)stack_top;
	register long x2 __asm__("x2") = 0;
	register long x3 __asm__("x3") = 0;
	register long x4 __asm__("x4") = 0;

	__asm__ volatile("svc #0\n\t"
			 "cbnz x0, 1f\n\t"
			 "blr %[fn]\n\t"
			 "mov x8, #93\n\t"
			 "mov x0, #0\n\t"
			 "svc #0\n\t"
			 "1:"
			 : "+r"(x0)
			 : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4),
			   [fn] "r"(fn)
			 : "x30", "memory");
	return x0;
#else
#error "futex selftest: no thread start for this ISA"
#endif
}

/* Runs in the sibling thread: wake the main thread, then report done. */
static void waker_thread(void)
{
	sleep_ns(DELAY_NS);
	g_word = 1;
	g_thread_wake_ret = futex(&g_word, LINUX_FUTEX_WAKE, 1);
	g_done = 1;
	(void)futex(&g_done, LINUX_FUTEX_WAKE, 1);
}

static void on_sigusr1(int sig)
{
	(void)sig;
	g_sigusr1_seen = 1;
}

/* FUTEX_WAIT on a word that no longer holds val fails with EAGAIN. */
static int wait_value_mismatch_eagain(void)
{
	long ret;

	g_word = 7;
	ret = futex(&g_word, LINUX_FUTEX_WAIT, 6);
	if (ret != -1 || errno != LINUX_EAGAIN)
	{
		ksft_print_num("futex_ret", ret);
		ksft_print_num("futex_errno", errno);
		return 0;
	}
	return 1;
}

/* FUTEX_WAKE without waiters wakes nobody. */
static int wake_without_waiters_returns_zero(void)
{
	long ret = futex(&g_word, LINUX_FUTEX_WAKE, 1);

	if (ret != 0)
	{
		ksft_print_num("wake_ret", ret);
		return 0;
	}
	return 1;
}

/* The main thread sleeps in FUTEX_WAIT until the sibling wakes it. */
static int wake_releases_waiting_thread(void)
{
	long tid;
	long ret = 0;

	g_word = 0;
	g_done = 0;
	tid = thread_start(waker_thread, g_thread_stack + THREAD_STACK);
	if (tid <= 0)
	{
		ksft_print_num("clone_ret", tid);
		return 0;
	}
	while (g_word == 0 && ret == 0)
		ret = futex(&g_word, LINUX_FUTEX_WAIT, 0);
	while (!g_done)
		(void)futex(&g_done, LINUX_FUTEX_WAIT, 0);
	if (ret != 0 || g_thread_wake_ret != 1)
	{
		ksft_print_num("wait_ret", ret);
		ksft_print_num("wait_errno", errno);
		ksft_print_num("thread_wake_ret", g_thread_wake_ret);
		return 0;
	}
	return 1;
}

/* A caught signal without SA_RESTART ends FUTEX_WAIT with EINTR. */
static int signal_interrupts_wait(void)
{
	struct sigaction sa = { 0 };
	int parent = getpid();
	int status = 0;
	long ret;
	int err;
	int pid;

	sa.sa_handler = on_sigusr1;
	if (sigaction(SIGUSR1, &sa, 0) != 0)
		return 0;
	pid = fork();
	if (pid < 0)
		return 0;
	if (pid == 0)
	{
		sleep_ns(DELAY_NS);
		kill(parent, SIGUSR1);
		_exit(0);
	}
	g_word = 0;
	ret = futex(&g_word, LINUX_FUTEX_WAIT, 0);
	err = errno;
	if (wait4(pid, &status, 0, 0) != pid)
		return 0;
	if (ret != -1 || err != LINUX_EINTR || !g_sigusr1_seen)
	{
		ksft_print_num("futex_ret", ret);
		ksft_print_num("futex_errno", err);
		ksft_print_num("handler_ran", g_sigusr1_seen);
		return 0;
	}
	return 1;
}

int main(void)
{
	ksft_set_plan(4);
	ksft_test_result(wait_value_mismatch_eagain(),
			 "wait_value_mismatch_eagain");
	ksft_test_result(wake_without_waiters_returns_zero(),
			 "wake_without_waiters_returns_zero");
	ksft_test_result(wake_releases_waiting_thread(),
			 "wake_releases_waiting_thread");
	ksft_test_result(signal_interrupts_wait(), "signal_interrupts_wait");
	return ksft_exit();
}
