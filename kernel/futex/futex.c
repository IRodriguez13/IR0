/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: futex.c
 * Description: futex wait queues: a hash of queue heads keyed by
 *              (address space, address) for private words and by physical
 *              address for MAP_SHARED ones (Linux kernel/futex/core.c,
 *              waitwake.c, requeue.c).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/clock.h>
#include <ir0/futex.h>
#include <ir0/clock_wait.h>
#include <ir0/copy_user.h>
#include <ir0/paging.h>
#include <ir0/process.h>
#include <ir0/sched.h>
#include <ir0/signals.h>

#define FUTEX_HASH_SIZE 64U
#define FUTEX_PAGE_MASK 0xfffUL

/*
 * A word on private memory is (mm, address): threads share an mm, other
 * processes never match. A shared futex on MAP_SHARED memory is its
 * physical address, the same in every process that maps it. As in Linux
 * get_futex_key(), a shared and a private futex on one private word are
 * different keys (FUT_OFF_MMSHARED): a shared wake misses private waiters.
 */
struct futex_key
{
	const void *space;
	uintptr_t word;
	unsigned int shared;
};

/*
 * A waiter lives on the sleeping task's kernel stack (Linux futex_q).
 * Wake, requeue, timeout, signal and exit unlink it; pprev is NULL once
 * it is off every queue.
 */
struct futex_q
{
	struct futex_q *next;
	struct futex_q **pprev;
	process_t *proc;
	struct futex_key key;
	uint32_t bitset;
	int woken;
};

static struct futex_q *futex_queues[FUTEX_HASH_SIZE];

/* Key space of MAP_SHARED words; only its address is used. */
static const char futex_shared_space;

void (*ir0_futex_before_sleep)(process_t *proc);

static struct futex_q **futex_bucket(const struct futex_key *key)
{
	uintptr_t h = (key->word >> 2) ^ ((uintptr_t)key->space >> 4);

	h ^= h >> 13;
	return &futex_queues[h % FUTEX_HASH_SIZE];
}

static int futex_key_eq(const struct futex_key *a, const struct futex_key *b)
{
	return a->space == b->space && a->word == b->word &&
	       a->shared == b->shared;
}

/*
 * futex_key_get - Key of @uaddr in @proc. A misaligned word is EINVAL and
 * a user process's word outside its VA window EFAULT. A shared futex
 * (IR0_FUTEX_SHARED) on a MAP_SHARED page that is mapped keys by physical
 * address; everything else keys by (mm, address).
 */
static int futex_key_get(process_t *proc, int *uaddr, unsigned int flags,
			 struct futex_key *key)
{
	uintptr_t addr = (uintptr_t)uaddr;
	uint64_t *pte;

	if (addr & (sizeof(int) - 1))
		return -EINVAL;
	if (proc->mode == USER_MODE && !access_ok(uaddr, sizeof(int)))
		return -EFAULT;

	key->space = proc->mm;
	key->word = addr;
	key->shared = flags & IR0_FUTEX_SHARED;
	if (!(flags & IR0_FUTEX_SHARED) || proc->mode != USER_MODE ||
	    !vma_is_shared(proc, addr))
		return 0;

	pte = paging_get_pte(process_pgd(proc), addr & ~FUTEX_PAGE_MASK);
	if (pte && pte_present(*pte))
	{
		key->space = &futex_shared_space;
		key->word = mm_pte_phys(*pte) + (addr & FUTEX_PAGE_MASK);
	}
	return 0;
}

/* Append @q to its key's bucket: waiters of one word wake in FIFO order. */
static void futex_q_link(struct futex_q *q)
{
	struct futex_q **pp = futex_bucket(&q->key);

	while (*pp)
		pp = &(*pp)->next;
	q->next = NULL;
	q->pprev = pp;
	*pp = q;
}

static void futex_q_unlink(struct futex_q *q)
{
	if (!q->pprev)
		return;
	*q->pprev = q->next;
	if (q->next)
		q->next->pprev = q->pprev;
	q->next = NULL;
	q->pprev = NULL;
}

static void futex_q_wake(struct futex_q *q)
{
	futex_q_unlink(q);
	q->woken = 1;
	process_set_sched_state(q->proc, PROCESS_READY);
	sched_promote(q->proc);
	clock_request_sched_resched();
}

/* Read the futex word: @proc is current, or a kernel thread's own word. */
static int futex_get_value(int *uaddr, int *val)
{
	return copy_from_user(val, uaddr, sizeof(*val));
}

int ir0_futex_wait(process_t *proc, int *uaddr, int val, unsigned int flags,
		   uint32_t bitset, uint64_t deadline_ms)
{
	struct futex_q q;
	int timed = deadline_ms != IR0_FUTEX_NO_TIMEOUT;
	int cur = 0;
	int ret;

	if (!proc || !uaddr || !bitset)
		return -EINVAL;
	/* Fault the word in first: a shared key needs its page mapped. */
	ret = futex_get_value(uaddr, &cur);
	if (ret == 0)
		ret = futex_key_get(proc, uaddr, flags, &q.key);
	if (ret)
		return ret;

	q.proc = proc;
	q.bitset = bitset;
	q.woken = 0;
	q.pprev = NULL;
	futex_q_link(&q);

	/*
	 * Linux futex_wait_setup(): queue first, then compare, so a waker
	 * that changes the word after our read finds us on the queue.
	 */
	ret = futex_get_value(uaddr, &cur);
	if (ret == 0 && cur != val)
		ret = -EAGAIN;
	if (ret)
	{
		futex_q_unlink(&q);
		return ret;
	}

	if (timed)
		ir0_clock_wait_arm(proc, deadline_ms);
	for (;;)
	{
		/*
		 * prepare_to_wait: BLOCKED before the tests, so a wake, the
		 * deadline or a signal in between sets READY instead of
		 * being lost.
		 */
		process_set_sched_state(proc, PROCESS_BLOCKED);
		if (q.woken)
		{
			ret = 0;
			break;
		}
		if (timed && clock_get_uptime_milliseconds() >= deadline_ms)
		{
			ret = -ETIMEDOUT;
			break;
		}
		if (signal_pending(proc))
		{
			/*
			 * Linux futex_wait(): an untimed wait restarts unless
			 * a handler without SA_RESTART runs. A timed one is
			 * restarted only when no handler runs; the deadline is
			 * absolute, so the restart keeps it for WAIT_BITSET.
			 */
			ret = timed ? -ERESTARTNOHAND : -ERESTARTSYS;
			break;
		}
		if (timed && !proc->clock_wait_armed)
			ir0_clock_wait_arm(proc, deadline_ms);
		if (ir0_futex_before_sleep)
			ir0_futex_before_sleep(proc);
		/* A wake since the tests already made us READY: test again. */
		if (proc->state == PROCESS_BLOCKED)
			schedule();
	}

	process_set_sched_state(proc, PROCESS_READY);
	if (timed)
		ir0_clock_wait_disarm(proc);
	futex_q_unlink(&q);
	return ret;
}

int ir0_futex_wake(process_t *proc, int *uaddr, unsigned int flags,
		   int nr_wake, uint32_t bitset)
{
	struct futex_key key;
	struct futex_q *q;
	struct futex_q *next;
	int woke = 0;
	int ret;

	if (!proc || !uaddr || !bitset)
		return -EINVAL;
	if ((flags & IR0_FUTEX_SHARED) && proc == current_process)
	{
		int unused = 0;

		/* Linux get_user_pages_fast(): map the page to key it. */
		(void)futex_get_value(uaddr, &unused);
	}
	ret = futex_key_get(proc, uaddr, flags, &key);
	if (ret)
		return ret;

	for (q = *futex_bucket(&key); q; q = next)
	{
		next = q->next;
		if (!futex_key_eq(&q->key, &key) || !(q->bitset & bitset))
			continue;
		futex_q_wake(q);
		/* Linux futex_wake(): nr_wake <= 0 still wakes one. */
		if (++woke >= nr_wake)
			break;
	}
	return woke;
}

int ir0_futex_requeue(process_t *proc, int *uaddr, int *uaddr2,
		      unsigned int flags, int nr_wake, int nr_requeue,
		      const int *cmpval)
{
	struct futex_key key1;
	struct futex_key key2;
	struct futex_q *q;
	struct futex_q *next;
	int count = 0;
	int cur = 0;
	int ret;

	if (!proc || !uaddr || !uaddr2)
		return -EINVAL;
	if (nr_wake < 0 || nr_requeue < 0)
		return -EINVAL;
	ret = futex_key_get(proc, uaddr, flags, &key1);
	if (ret == 0)
		ret = futex_key_get(proc, uaddr2, flags, &key2);
	if (ret)
		return ret;
	if (cmpval)
	{
		ret = futex_get_value(uaddr, &cur);
		if (ret)
			return ret;
		if (cur != *cmpval)
			return -EAGAIN;
	}

	/*
	 * Linux futex_requeue(): the first nr_wake waiters wake, the next
	 * nr_requeue move to uaddr2; the result counts both. A waiter moved
	 * to another bucket's tail has key2 and is skipped if met again.
	 */
	for (q = *futex_bucket(&key1); q; q = next)
	{
		next = q->next;
		if (count - nr_wake >= nr_requeue)
			break;
		if (!futex_key_eq(&q->key, &key1))
			continue;
		if (++count <= nr_wake)
		{
			futex_q_wake(q);
			continue;
		}
		if (!futex_key_eq(&key1, &key2))
		{
			futex_q_unlink(q);
			q->key = key2;
			futex_q_link(q);
		}
	}
	return count;
}

void ir0_futex_drop_process(process_t *proc)
{
	unsigned int i;
	struct futex_q *q;
	struct futex_q *next;

	if (!proc)
		return;
	for (i = 0; i < FUTEX_HASH_SIZE; i++)
	{
		for (q = futex_queues[i]; q; q = next)
		{
			next = q->next;
			if (q->proc == proc)
				futex_q_unlink(q);
		}
	}
}
