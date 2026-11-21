/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscalls.c
 * Description: futex(2), set_robust_list(2) and get_robust_list(2), and the
 *              robust-list walk at exit (Linux kernel/futex/syscalls.c and
 *              core.c exit_robust_list()).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <kernel/syscalls/process_syscalls.h>
#include <ir0/clock_wait.h>
#include <ir0/copy_user.h>
#include <ir0/futex.h>
#include <ir0/ktm/klog.h>

/* Linux ROBUST_LIST_LIMIT: bounds the walk of a corrupt or cyclic list. */
#define ROBUST_LIST_LIMIT 2048

/* Robust futex list (set_robust_list / get_robust_list / exit walk). */
struct robust_list
{
	struct robust_list *next;
};

struct robust_list_head
{
	struct robust_list list;
	long futex_offset;
	struct robust_list *list_op_pending;
};

int64_t sys_set_robust_list(struct robust_list_head *head, size_t len)
{
	if (!current_process)
		return -ESRCH;

	if (len != sizeof(struct robust_list_head))
		return -EINVAL;

	if (head && current_process->mode == USER_MODE)
	{
		if (!access_ok(head, sizeof(struct robust_list_head)))
			return -EFAULT;
	}

	current_process->robust_list = head;
	return 0;
}

int64_t sys_get_robust_list(int pid, struct robust_list_head **head_ptr,
			    size_t *len_ptr)
{
	process_t *target = current_process;
	struct robust_list_head *head;
	size_t len = sizeof(struct robust_list_head);

	if (!current_process)
		return -ESRCH;
	if (pid != 0 && pid != (int)current_process->task.pid)
		return -ESRCH;
	if (!head_ptr || !len_ptr)
		return -EFAULT;

	/* Linux get_robust_list(): the length is written first. */
	head = target->robust_list;
	if (copy_to_user(len_ptr, &len, sizeof(len)) != 0)
		return -EFAULT;
	if (copy_to_user(head_ptr, &head, sizeof(head)) != 0)
		return -EFAULT;
	return 0;
}

/*
 * Linux handle_futex_death(): the dying owner @tid still holds the word
 * at @uaddr. Mark it FUTEX_OWNER_DIED, keep FUTEX_WAITERS and wake one
 * waiter if there are any. A pending op on a word nobody owns yet (0)
 * still wakes one waiter, as the owner may have died between the
 * unlock and the wake.
 */
static void robust_futex_death(process_t *p, address_space_root_t root,
			       uintptr_t uaddr, uint32_t tid, int pending)
{
	uint32_t uval;
	uint32_t mval;

	if (uaddr & (sizeof(uint32_t) - 1))
		return;
	if (copy_from_user_mm(root, uaddr, &uval, sizeof(uval)) != 0)
		return;
	if (pending && uval == 0)
	{
		(void)ir0_futex_wake(p, (int *)uaddr, IR0_FUTEX_SHARED, 1,
				     FUTEX_BITSET_MATCH_ANY);
		return;
	}
	if ((uval & FUTEX_TID_MASK) != tid)
		return;
	mval = (uval & FUTEX_WAITERS) | FUTEX_OWNER_DIED;
	if (copy_to_user_mm(root, uaddr, &mval, sizeof(mval)) != 0)
		return;
	if (uval & FUTEX_WAITERS)
		(void)ir0_futex_wake(p, (int *)uaddr, IR0_FUTEX_SHARED, 1,
				     FUTEX_BITSET_MATCH_ANY);
}

/* Entry pointers carry the PI flag in bit 0 (PI futexes are not supported). */
static uintptr_t robust_entry(uintptr_t ptr)
{
	return ptr & ~(uintptr_t)1;
}

/*
 * process_exit_robust_list - Linux exit_robust_list(): every robust futex
 * that @p still holds gets FUTEX_OWNER_DIED and one waiter woken; the
 * list_op_pending entry is handled last. All reads and writes go through
 * @p's address space, so @p need not be current.
 */
void process_exit_robust_list(process_t *p)
{
	struct robust_list_head kh;
	address_space_root_t root;
	uintptr_t head;
	uintptr_t entry;
	uintptr_t pending;
	uintptr_t next;
	uint32_t tid;
	int limit = ROBUST_LIST_LIMIT;

	if (!p || !p->robust_list)
		return;
	head = (uintptr_t)p->robust_list;
	p->robust_list = NULL;
	if (p->mode != USER_MODE)
		return;
	root = process_pgd(p);
	if (copy_from_user_mm(root, head, &kh, sizeof(kh)) != 0)
		return;

	tid = (uint32_t)p->task.pid;
	entry = robust_entry((uintptr_t)kh.list.next);
	pending = robust_entry((uintptr_t)kh.list_op_pending);
	while (entry != head)
	{
		if (copy_from_user_mm(root, entry, &next, sizeof(next)) != 0)
			return;
		if (entry != pending)
			robust_futex_death(p, root, entry + kh.futex_offset, tid,
					   0);
		entry = robust_entry(next);
		if (--limit == 0)
			break;
	}
	if (pending)
		robust_futex_death(p, root, pending + kh.futex_offset, tid, 1);
	klog_smoke("ROBUST_LIST_EXIT_OK");
}

/*
 * futex(2) (Linux do_futex()). FUTEX_PRIVATE_FLAG keys the word by
 * address space only; FUTEX_CLOCK_REALTIME puts a WAIT_BITSET deadline on
 * CLOCK_REALTIME and is ENOSYS with the non-wait ops. FUTEX_WAIT takes a
 * relative timeout, FUTEX_WAIT_BITSET an absolute one. REQUEUE and
 * CMP_REQUEUE take nr_requeue in the timeout slot. The PI ops and
 * FUTEX_WAKE_OP are ENOSYS.
 */
int64_t sys_futex(int *uaddr, int op, int val, const struct timespec *timeout,
		  int *uaddr2, int val3)
{
	unsigned int flags = (op & FUTEX_PRIVATE_FLAG) ? 0 : IR0_FUTEX_SHARED;
	int cmd = op & FUTEX_CMD_MASK;
	uint64_t deadline = IR0_FUTEX_NO_TIMEOUT;
	uint32_t bitset = FUTEX_BITSET_MATCH_ANY;
	struct timespec ts;
	int ret;

	if (!current_process)
		return -ESRCH;

	switch (cmd)
	{
	case FUTEX_WAIT_BITSET:
		bitset = (uint32_t)val3;
		/* fallthrough */
	case FUTEX_WAIT:
		if (timeout)
		{
			if (copy_from_user(&ts, timeout, sizeof(ts)) != 0)
				return -EFAULT;
			ret = ir0_clock_deadline_ms(
				&ts, cmd == FUTEX_WAIT_BITSET,
				(op & FUTEX_CLOCK_REALTIME) != 0, &deadline);
			if (ret)
				return ret;
		}
		return ir0_futex_wait(current_process, uaddr, val, flags,
				      bitset, deadline);
	case FUTEX_WAKE_BITSET:
		bitset = (uint32_t)val3;
		/* fallthrough */
	case FUTEX_WAKE:
		if (op & FUTEX_CLOCK_REALTIME)
			return -ENOSYS;
		return ir0_futex_wake(current_process, uaddr, flags, val,
				      bitset);
	case FUTEX_REQUEUE:
	case FUTEX_CMP_REQUEUE:
		if (op & FUTEX_CLOCK_REALTIME)
			return -ENOSYS;
		return ir0_futex_requeue(current_process, uaddr, uaddr2, flags,
					 val, (int)(uintptr_t)timeout,
					 cmd == FUTEX_CMP_REQUEUE ? &val3 :
								    NULL);
	default:
		return -ENOSYS;
	}
}
