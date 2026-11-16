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
 *              robust-list walk at exit (Linux kernel/futex/syscalls.c).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <kernel/syscalls/process_syscalls.h>
#include <ir0/copy_user.h>
#include <ir0/futex.h>
#include <ir0/ktm/klog.h>

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

/* Robust futex list (set_robust_list / get_robust_list / exit walk). */
struct robust_list_head
{
	void *list;
	long futex_offset;
	void *list_op_pending;
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

/**
 * Best-effort robust futex exit: wake one waiter on the pending/list head
 * word if present (Linux FUTEX_OWNER_DIED subset deferred to full walk).
 */
void process_exit_robust_list(process_t *p)
{
	struct robust_list_head kh;
	int *uaddr;
	int uval;
	int copy_ret;

	if (!p || !p->robust_list)
		return;

	if (p->mode == USER_MODE)
	{
		copy_ret = copy_from_user_mm(process_pgd(p),
					     (uintptr_t)p->robust_list, &kh,
					     sizeof(kh));
		if (copy_ret != 0)
		{
			p->robust_list = NULL;
			return;
		}
	}
	else
	{
		kh = *p->robust_list;
	}

	uaddr = (int *)kh.list_op_pending;
	if (!uaddr)
		uaddr = (int *)kh.list;
	/*
	 * Linux handle_futex_death(): read the futex word through the dying
	 * task's mm first; a word it cannot read wakes nobody.
	 */
	if (uaddr && p->mode == USER_MODE &&
	    copy_from_user_mm(process_pgd(p), (uintptr_t)uaddr, &uval,
			      sizeof(uval)) == 0)
		(void)ir0_futex_wake(uaddr, 1);

	p->robust_list = NULL;
	klog_smoke("ROBUST_LIST_EXIT_OK");
}

int64_t sys_futex(int *uaddr, int op, int val, const struct timespec *timeout,
		  int *uaddr2, int val3)
{
	int cur;
	int cmd;

	(void)timeout;
	(void)uaddr2;
	(void)val3;

	if (!current_process)
		return -ESRCH;

	cmd = op & 0x7f; /* strip FUTEX_PRIVATE_FLAG / CLOCK_REALTIME bits */

	/*
	 * FUTEX_WAKE reads nothing at @uaddr, so it checks the range itself;
	 * FUTEX_WAIT's copy does. Linux do_futex(): an unknown op is ENOSYS.
	 */
	if (cmd == FUTEX_WAKE)
	{
		if (uaddr && !access_ok(uaddr, sizeof(int)))
			return -EFAULT;
		return (int64_t)ir0_futex_wake(uaddr, val);
	}

	if (cmd == FUTEX_WAIT)
	{
		if (copy_from_user(&cur, uaddr, sizeof(cur)) != 0)
			return -EFAULT;
		if (cur != val)
			return -EAGAIN;
		return (int64_t)ir0_futex_wait(current_process, uaddr, val);
	}

	return -ENOSYS;
}
