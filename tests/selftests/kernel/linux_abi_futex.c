/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: linux_abi_futex.c
 * Description: Selftest: futex(2) as on Linux (kernel/futex/): timeouts,
 *              the bitset ops, requeue, private keys per address space and
 *              an unbounded number of waiters. On dev 26f89a5b the timeout
 *              was ignored, the bitset and requeue ops were ENOSYS, waiters
 *              were capped at 64 and keyed by virtual address alone. Every
 *              case passes on Linux. Run by make ir0-selftest-futex-abi-
 *              {x86,arm64}.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "conf.h"

#include <sys/mman.h>
#include <time.h>

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_REQUEUE 3
#define FUTEX_CMP_REQUEUE 4
#define FUTEX_WAIT_BITSET 9
#define FUTEX_WAKE_BITSET 10
#define FUTEX_PRIVATE 128
#define FUTEX_CLOCK_RT 256
#define BITSET_ANY 0xffffffffU
#define LINUX_CLOCK_REALTIME 0
#define LINUX_CLOCK_MONOTONIC 1
#define CLONE_THREAD_FLAGS 0x10f00UL /* CLONE_VM|FS|FILES|SIGHAND|THREAD */
#define THREAD_STACK 8192UL
#define MAX_THREADS 80
#define TIMEOUT_MS 100L
/* An idle guest wakes within this bound; it only catches "never slept". */
#define SLACK_MS 1000L
/* Waiters still asleep after this are a failure, not a hang. */
#define WAITER_TIMEOUT_MS 5000L

static volatile int g_word;
static volatile int g_word2;
static volatile int g_ready;
static volatile int g_done;
static volatile long g_ret[MAX_THREADS];
static volatile unsigned int g_wait_bitset;

static long futex(volatile int *uaddr, int op, int val,
		  const struct timespec *ts, volatile int *uaddr2, int val3)
{
	return S6(SYS_futex, uaddr, op, val, ts, uaddr2, val3);
}

static void ts_set_ms(struct timespec *ts, long ms)
{
	ts->tv_sec = ms / 1000L;
	ts->tv_nsec = (ms % 1000L) * 1000000L;
}

static long clock_ms(int clock)
{
	struct timespec ts;

	if (S2(SYS_clock_gettime, clock, &ts) != 0)
		return -1;
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/*
 * Start fn on @stack_top in a thread of this process; the thread exits
 * with SYS_exit when fn returns. Returns the thread id or -errno. The
 * child pops fn from its new stack, so it needs no register but the
 * syscall result.
 */
static long thread_start(void (*fn)(void), void *stack_top)
{
	void **sp = (void **)stack_top - 2;

	sp[0] = (void *)fn;
#if defined(__x86_64__)
	register long r10 __asm__("r10") = 0;
	register long r8 __asm__("r8") = 0;
	long ret;

	__asm__ volatile("syscall\n\t"
			 "test %%rax, %%rax\n\t"
			 "jnz 1f\n\t"
			 "pop %%rax\n\t"
			 "pop %%rcx\n\t"
			 "call *%%rax\n\t"
			 "mov $60, %%eax\n\t"
			 "xor %%edi, %%edi\n\t"
			 "syscall\n\t"
			 "1:"
			 : "=a"(ret)
			 : "0"((long)SYS_clone), "D"(CLONE_THREAD_FLAGS),
			   "S"(sp), "d"(0L), "r"(r10), "r"(r8)
			 : "rcx", "r11", "memory");
	return ret;
#elif defined(__aarch64__)
	register long x8 __asm__("x8") = SYS_clone;
	register long x0 __asm__("x0") = (long)CLONE_THREAD_FLAGS;
	register long x1 __asm__("x1") = (long)sp;
	register long x2 __asm__("x2") = 0;
	register long x3 __asm__("x3") = 0;
	register long x4 __asm__("x4") = 0;

	__asm__ volatile("svc #0\n\t"
			 "cbnz x0, 1f\n\t"
			 "ldr x9, [sp], #16\n\t"
			 "blr x9\n\t"
			 "mov x8, #93\n\t"
			 "mov x0, #0\n\t"
			 "svc #0\n\t"
			 "1:"
			 : "+r"(x0)
			 : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4)
			 : "x9", "x30", "memory");
	return x0;
#else
#error "futex selftest: no thread start for this ISA"
#endif
}

/* Start @n threads running @fn; each gets its own stack. */
static long threads_start(void (*fn)(void), int n)
{
	unsigned long len = THREAD_STACK * (unsigned long)n;
	long base = S6(SYS_mmap, 0, len, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	long ret;
	int i;

	if (base < 0 && base > -4096)
		return base;
	g_ready = 0;
	g_done = 0;
	for (i = 0; i < n; i++)
	{
		g_ret[i] = -1000;
		ret = thread_start(fn, (void *)(base + THREAD_STACK * (i + 1)));
		if (ret < 0)
			return ret;
	}
	return 0;
}

/* Spin-free wait (sleeps) until @counter reaches @n; 0 or -ETIMEDOUT. */
static long wait_count(volatile int *counter, int n, long timeout_ms)
{
	long start = rt_now_ms();

	while (__atomic_load_n(counter, __ATOMIC_SEQ_CST) < n)
	{
		if (rt_now_ms() - start > timeout_ms)
			return -ETIMEDOUT;
		rt_sleep_ms(5);
	}
	return 0;
}

/* Waiter body: the thread's index is its ready-order slot. */
static int thread_slot(void)
{
	return __atomic_fetch_add(&g_ready, 1, __ATOMIC_SEQ_CST);
}

static void thread_finish(int slot, long ret)
{
	g_ret[slot] = ret;
	__atomic_fetch_add(&g_done, 1, __ATOMIC_SEQ_CST);
}

/* FUTEX_WAIT on g_word (value 0) with a bounded timeout. */
static void waiter_word(void)
{
	struct timespec ts;
	int slot = thread_slot();

	ts_set_ms(&ts, WAITER_TIMEOUT_MS);
	thread_finish(slot, futex(&g_word, FUTEX_WAIT | FUTEX_PRIVATE, 0, &ts,
				  0, 0));
}

/* FUTEX_WAIT_BITSET on g_word with g_wait_bitset, absolute deadline. */
static void waiter_bitset(void)
{
	struct timespec ts;
	int slot = thread_slot();

	ts_set_ms(&ts, clock_ms(LINUX_CLOCK_MONOTONIC) + WAITER_TIMEOUT_MS);
	thread_finish(slot, futex(&g_word, FUTEX_WAIT_BITSET | FUTEX_PRIVATE, 0,
				  &ts, 0, (int)g_wait_bitset));
}

/*
 * FUTEX_WAIT with a relative timeout that expires fails with ETIMEDOUT
 * after the interval; a malformed timeout is EINVAL.
 */
int futex_wait_timeout_etimedout(void)
{
	struct timespec ts;
	long t0;
	long dt;

	g_word = 0;
	ts_set_ms(&ts, TIMEOUT_MS);
	t0 = rt_now_ms();
	EXPECT_ERR(futex(&g_word, FUTEX_WAIT | FUTEX_PRIVATE, 0, &ts, 0, 0),
		   ETIMEDOUT);
	dt = rt_now_ms() - t0;
	CHECK(dt >= TIMEOUT_MS && dt < TIMEOUT_MS + SLACK_MS);

	ts_set_ms(&ts, TIMEOUT_MS);
	t0 = rt_now_ms();
	EXPECT_ERR(futex(&g_word, FUTEX_WAIT, 0, &ts, 0, 0), ETIMEDOUT);
	dt = rt_now_ms() - t0;
	CHECK(dt >= TIMEOUT_MS && dt < TIMEOUT_MS + SLACK_MS);

	ts.tv_sec = 0;
	ts.tv_nsec = 1000000000L;
	EXPECT_ERR(futex(&g_word, FUTEX_WAIT | FUTEX_PRIVATE, 0, &ts, 0, 0),
		   EINVAL);
	/* The value check comes first: a changed word is EAGAIN. */
	ts_set_ms(&ts, TIMEOUT_MS);
	EXPECT_ERR(futex(&g_word, FUTEX_WAIT | FUTEX_PRIVATE, 1, &ts, 0, 0),
		   EAGAIN);
	return CONF_PASS;
}

/*
 * FUTEX_WAIT_BITSET takes an absolute deadline on CLOCK_MONOTONIC, or on
 * CLOCK_REALTIME with FUTEX_CLOCK_REALTIME; a past deadline times out at
 * once and a zero bitset is EINVAL.
 */
int futex_wait_bitset_absolute(void)
{
	struct timespec ts;
	long target;

	g_word = 0;
	target = clock_ms(LINUX_CLOCK_MONOTONIC) + TIMEOUT_MS;
	ts_set_ms(&ts, target);
	EXPECT_ERR(futex(&g_word, FUTEX_WAIT_BITSET | FUTEX_PRIVATE, 0, &ts, 0,
			 (int)BITSET_ANY), ETIMEDOUT);
	CHECK(clock_ms(LINUX_CLOCK_MONOTONIC) >= target);
	CHECK(clock_ms(LINUX_CLOCK_MONOTONIC) < target + SLACK_MS);

	target = clock_ms(LINUX_CLOCK_REALTIME) + TIMEOUT_MS;
	ts_set_ms(&ts, target);
	EXPECT_ERR(futex(&g_word,
			 FUTEX_WAIT_BITSET | FUTEX_PRIVATE | FUTEX_CLOCK_RT, 0,
			 &ts, 0, (int)BITSET_ANY), ETIMEDOUT);
	CHECK(clock_ms(LINUX_CLOCK_REALTIME) >= target);

	ts_set_ms(&ts, 1);
	EXPECT_ERR(futex(&g_word, FUTEX_WAIT_BITSET | FUTEX_PRIVATE, 0, &ts, 0,
			 (int)BITSET_ANY), ETIMEDOUT);
	EXPECT_ERR(futex(&g_word, FUTEX_WAIT_BITSET | FUTEX_PRIVATE, 0, &ts, 0,
			 0), EINVAL);
	EXPECT_ERR(futex(&g_word, FUTEX_WAKE_BITSET | FUTEX_PRIVATE, 1, 0, 0, 0),
		   EINVAL);
	/* FUTEX_CLOCK_REALTIME only goes with the wait ops. */
	EXPECT_ERR(futex(&g_word, FUTEX_WAKE | FUTEX_CLOCK_RT, 1, 0, 0, 0),
		   ENOSYS);
	return CONF_PASS;
}

/*
 * FUTEX_WAKE_BITSET wakes only waiters whose bitset intersects its own;
 * plain FUTEX_WAKE matches every bitset.
 */
int futex_bitset_mismatch_not_woken(void)
{
	long woke = 0;
	long start;

	g_word = 0;
	g_wait_bitset = 0x1;
	EXPECT(threads_start(waiter_bitset, 1), 0);
	EXPECT(wait_count(&g_ready, 1, WAITER_TIMEOUT_MS), 0);
	rt_sleep_ms(50);
	EXPECT(futex(&g_word, FUTEX_WAKE_BITSET | FUTEX_PRIVATE, 1, 0, 0, 0x2),
	       0);
	EXPECT(g_done, 0);
	start = rt_now_ms();
	while (woke == 0 && rt_now_ms() - start < WAITER_TIMEOUT_MS)
		woke = futex(&g_word, FUTEX_WAKE_BITSET | FUTEX_PRIVATE, 1, 0,
			     0, 0x3);
	EXPECT(woke, 1);
	EXPECT(wait_count(&g_done, 1, WAITER_TIMEOUT_MS), 0);
	EXPECT(g_ret[0], 0);

	g_wait_bitset = 0x4;
	EXPECT(threads_start(waiter_bitset, 1), 0);
	EXPECT(wait_count(&g_ready, 1, WAITER_TIMEOUT_MS), 0);
	rt_sleep_ms(50);
	EXPECT(futex(&g_word, FUTEX_WAKE | FUTEX_PRIVATE, 1, 0, 0, 0), 1);
	EXPECT(wait_count(&g_done, 1, WAITER_TIMEOUT_MS), 0);
	EXPECT(g_ret[0], 0);
	return CONF_PASS;
}

/*
 * FUTEX_REQUEUE wakes nr_wake waiters of uaddr and moves up to nr_requeue
 * (passed in the timeout slot) of the rest to uaddr2; it returns both
 * counts together. The moved waiters then wake through uaddr2 only.
 */
int futex_requeue_moves_waiters(void)
{
	int i;

	g_word = 0;
	g_word2 = 0;
	EXPECT(threads_start(waiter_word, 3), 0);
	EXPECT(wait_count(&g_ready, 3, WAITER_TIMEOUT_MS), 0);
	rt_sleep_ms(100);
	EXPECT(futex(&g_word, FUTEX_REQUEUE | FUTEX_PRIVATE, 1,
		     (const struct timespec *)2L, &g_word2, 0), 3);
	EXPECT(wait_count(&g_done, 1, WAITER_TIMEOUT_MS), 0);
	EXPECT(futex(&g_word, FUTEX_WAKE | FUTEX_PRIVATE, 10, 0, 0, 0), 0);
	EXPECT(futex(&g_word2, FUTEX_WAKE | FUTEX_PRIVATE, 10, 0, 0, 0), 2);
	EXPECT(wait_count(&g_done, 3, WAITER_TIMEOUT_MS), 0);
	for (i = 0; i < 3; i++)
		EXPECT(g_ret[i], 0);
	return CONF_PASS;
}

/*
 * FUTEX_CMP_REQUEUE fails with EAGAIN when *uaddr is not val3, and a
 * negative count is EINVAL.
 */
int futex_cmp_requeue_checks_value(void)
{
	g_word = 5;
	g_word2 = 0;
	EXPECT_ERR(futex(&g_word, FUTEX_CMP_REQUEUE | FUTEX_PRIVATE, 1,
			 (const struct timespec *)1L, &g_word2, 4), EAGAIN);
	EXPECT(futex(&g_word, FUTEX_CMP_REQUEUE | FUTEX_PRIVATE, 1,
		     (const struct timespec *)1L, &g_word2, 5), 0);
	EXPECT_ERR(futex(&g_word, FUTEX_CMP_REQUEUE | FUTEX_PRIVATE, -1,
			 (const struct timespec *)1L, &g_word2, 5), EINVAL);
	return CONF_PASS;
}

/*
 * A futex on private memory belongs to one address space: a forked child
 * waiting at the same address is not woken by the parent, with or without
 * FUTEX_PRIVATE_FLAG. On MAP_SHARED memory a shared futex does cross.
 */
int futex_private_isolated_across_processes(void)
{
	volatile int *shared;
	int status = 0;
	long pid;
	long woke;
	long start;
	long i;

	g_word = 0;
	pid = rt_fork();
	if (pid == 0)
	{
		struct timespec ts;

		ts_set_ms(&ts, 400);
		rt_exit(futex(&g_word, FUTEX_WAIT | FUTEX_PRIVATE, 0, &ts, 0,
			      0) == -ETIMEDOUT ? 0 : 1);
	}
	CHECK(pid > 0);
	rt_sleep_ms(100);
	woke = futex(&g_word, FUTEX_WAKE | FUTEX_PRIVATE, 1, 0, 0, 0);
	woke += futex(&g_word, FUTEX_WAKE, 1, 0, 0, 0);
	EXPECT(conf_child_status(pid, &status, WAITER_TIMEOUT_MS), pid);
	EXPECT(woke, 0);
	CHECK(RT_EXITED(status));
	EXPECT(RT_EXITCODE(status), 0);

	OK(i, S6(SYS_mmap, 0, RT_PAGE, PROT_READ | PROT_WRITE,
		 MAP_SHARED | MAP_ANONYMOUS, -1, 0));
	shared = (volatile int *)i;
	*shared = 0;
	pid = rt_fork();
	if (pid == 0)
	{
		struct timespec ts;

		ts_set_ms(&ts, WAITER_TIMEOUT_MS);
		rt_exit(futex(shared, FUTEX_WAIT, 0, &ts, 0, 0) == 0 ? 0 : 1);
	}
	CHECK(pid > 0);
	rt_sleep_ms(100);
	woke = 0;
	start = rt_now_ms();
	while (woke == 0 && rt_now_ms() - start < WAITER_TIMEOUT_MS)
		woke = futex(shared, FUTEX_WAKE, 1, 0, 0, 0);
	EXPECT(conf_child_status(pid, &status, WAITER_TIMEOUT_MS), pid);
	EXPECT(woke, 1);
	CHECK(RT_EXITED(status));
	EXPECT(RT_EXITCODE(status), 0);
	return CONF_PASS;
}

/* There is no fixed waiter limit: one FUTEX_WAKE wakes all 80 waiters. */
int futex_wake_more_than_64_waiters(void)
{
	long woke = 0;
	long start;
	int i;

	g_word = 0;
	EXPECT(threads_start(waiter_word, MAX_THREADS), 0);
	EXPECT(wait_count(&g_ready, MAX_THREADS, WAITER_TIMEOUT_MS), 0);
	rt_sleep_ms(200);
	EXPECT(g_done, 0);
	start = rt_now_ms();
	while (woke < MAX_THREADS && rt_now_ms() - start < WAITER_TIMEOUT_MS)
		woke += futex(&g_word, FUTEX_WAKE | FUTEX_PRIVATE, 0x7fffffff,
			      0, 0, 0);
	EXPECT(woke, MAX_THREADS);
	EXPECT(wait_count(&g_done, MAX_THREADS, WAITER_TIMEOUT_MS), 0);
	for (i = 0; i < MAX_THREADS; i++)
		EXPECT(g_ret[i], 0);
	return CONF_PASS;
}

/* set_robust_list(2) layout: an entry list and the futex word offset. */
struct robust_entry
{
	struct robust_entry *next;
	int futex;
};

struct robust_head
{
	struct robust_entry *list;
	long futex_offset;
	struct robust_entry *list_op_pending;
};

#define FUTEX_WAITERS_BIT 0x80000000U
#define FUTEX_OWNER_DIED_BIT 0x40000000U

static struct robust_head g_robust_head;
static struct robust_entry g_robust_entry;

/* Hold the robust futex g_robust_entry.futex, then exit holding it. */
static void robust_owner(void)
{
	int slot = thread_slot();

	g_robust_head.list = &g_robust_entry;
	g_robust_head.futex_offset = (long)offsetof(struct robust_entry, futex);
	g_robust_head.list_op_pending = 0;
	g_robust_entry.next = (struct robust_entry *)&g_robust_head;
	__atomic_store_n(&g_robust_entry.futex,
			 (int)(FUTEX_WAITERS_BIT | (unsigned int)S0(SYS_gettid)),
			 __ATOMIC_SEQ_CST);
	g_ret[slot] = S2(SYS_set_robust_list, &g_robust_head,
			 sizeof(g_robust_head));
	__atomic_fetch_add(&g_done, 1, __ATOMIC_SEQ_CST);
	rt_sleep_ms(100);
}

/*
 * Linux exit_robust_list(): a thread that exits holding a robust futex
 * with waiters leaves FUTEX_WAITERS | FUTEX_OWNER_DIED in the word and
 * wakes one shared-futex waiter.
 */
int futex_robust_owner_died_wakes(void)
{
	struct timespec ts;
	int held;

	g_robust_entry.futex = 0;
	EXPECT(threads_start(robust_owner, 1), 0);
	EXPECT(wait_count(&g_done, 1, WAITER_TIMEOUT_MS), 0);
	EXPECT(g_ret[0], 0);
	held = __atomic_load_n(&g_robust_entry.futex, __ATOMIC_SEQ_CST);
	CHECK(held & (int)FUTEX_WAITERS_BIT);
	ts_set_ms(&ts, WAITER_TIMEOUT_MS);
	/* The exit wake is a shared futex wake: a private waiter misses it. */
	EXPECT(futex(&g_robust_entry.futex, FUTEX_WAIT, held, &ts, 0, 0), 0);
	EXPECT((unsigned int)g_robust_entry.futex,
	       FUTEX_WAITERS_BIT | FUTEX_OWNER_DIED_BIT);
	return CONF_PASS;
}
