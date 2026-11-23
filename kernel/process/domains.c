/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: domains.c
 * Description: Explicit clone/init for process credential/signal/MM domains.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <ir0/process_domains.h>
#include <ir0/clone.h>
#include <ir0/errno.h>
#include <ir0/spinlock.h>
#include <string.h>

static ir0_spinlock_t sighand_lock;

static struct sighand_struct *sighand_alloc_default(void)
{
	struct sighand_struct *hand;
	int i;

	hand = kmalloc(sizeof(*hand));
	if (!hand)
		return NULL;
	hand->refcount = 1;
	for (i = 0; i <= _NSIG; i++)
	{
		hand->handlers[i] = SIG_DFL;
		hand->sa_flags[i] = 0;
		hand->sa_mask[i] = 0;
		hand->restorer[i] = NULL;
	}
	return hand;
}

static struct sighand_struct *sighand_alloc_copy(const struct sighand_struct *src)
{
	struct sighand_struct *hand;

	if (!src)
		return NULL;
	hand = kmalloc(sizeof(*hand));
	if (!hand)
		return NULL;
	memcpy(hand, src, sizeof(*hand));
	hand->refcount = 1;
	return hand;
}

static struct sighand_struct *sighand_get(struct sighand_struct *hand)
{
	uint64_t irq_flags;

	if (!hand)
		return NULL;
	irq_flags = ir0_spin_lock_irqsave(&sighand_lock);
	hand->refcount++;
	ir0_spin_unlock_irqrestore(&sighand_lock, irq_flags);
	return hand;
}

static void sighand_put(struct sighand_struct *hand)
{
	uint64_t irq_flags;
	int last;

	if (!hand)
		return;
	irq_flags = ir0_spin_lock_irqsave(&sighand_lock);
	if (hand->refcount <= 0)
	{
		ir0_spin_unlock_irqrestore(&sighand_lock, irq_flags);
		panic("sighand_put: refcount underflow");
		return;
	}
	hand->refcount--;
	last = (hand->refcount == 0);
	ir0_spin_unlock_irqrestore(&sighand_lock, irq_flags);
	if (last)
		kfree(hand);
}

static int sighand_users(const struct sighand_struct *hand)
{
	uint64_t irq_flags;
	int n;

	if (!hand)
		return 0;
	irq_flags = ir0_spin_lock_irqsave(&sighand_lock);
	n = hand->refcount;
	ir0_spin_unlock_irqrestore(&sighand_lock, irq_flags);
	return n;
}

int process_signals_init(process_t *p)
{
	if (!p)
		return -EINVAL;
	p->signal_pending = 0;
	p->signal_mask = 0;
	p->signal_ignored = 0;
	p->signal_mask_saved = 0;
	p->signal_mask_saved_valid = 0;
	p->saved_sigmask = 0;
	p->restore_sigmask = 0;
	p->signal_frame_sp = 0;
	p->sighand = sighand_alloc_default();
	if (!p->sighand)
		return -ENOMEM;
	return 0;
}

void process_signals_drop(process_t *p)
{
	struct sighand_struct *hand;

	if (!p)
		return;
	hand = p->sighand;
	p->sighand = NULL;
	sighand_put(hand);
}

int process_signals_unshare(process_t *p)
{
	struct sighand_struct *old;
	struct sighand_struct *copy;

	if (!p || !p->sighand)
		return -EINVAL;
	if (sighand_users(p->sighand) <= 1)
		return 0;
	copy = sighand_alloc_copy(p->sighand);
	if (!copy)
		return -ENOMEM;
	old = p->sighand;
	p->sighand = copy;
	sighand_put(old);
	return 0;
}

int process_cred_clone(process_t *dst, const process_t *src)
{
	if (!dst || !src)
		return -EINVAL;

	dst->uid = src->uid;
	dst->gid = src->gid;
	dst->euid = src->euid;
	dst->egid = src->egid;
	dst->suid = src->suid;
	dst->sgid = src->sgid;
	dst->umask = src->umask;
	dst->ngroups = src->ngroups;
	memcpy(dst->groups, src->groups, sizeof(dst->groups));
	dst->no_new_privs = src->no_new_privs;
	dst->dumpable = src->dumpable;
	dst->keep_caps = src->keep_caps;
	dst->at_secure = src->at_secure;
	return 0;
}

int process_signals_clone(process_t *dst, const process_t *src,
			  unsigned long flags)
{
	if (!dst || !src)
		return -EINVAL;

	dst->signal_pending = 0;
	dst->signal_mask = src->signal_mask;
	dst->signal_ignored = src->signal_ignored;
	dst->signal_mask_saved = 0;
	dst->signal_mask_saved_valid = 0;
	dst->signal_frame_sp = 0;
	/*
	 * Alternate signal stack is per task. fork copies it. A thread that
	 * shares the address space must not deliver onto the parent's stack.
	 */
	if ((flags & (CLONE_VM | CLONE_VFORK)) == CLONE_VM)
	{
		dst->sas_ss_sp = 0;
		dst->sas_ss_size = 0;
		dst->sas_ss_flags = SS_DISABLE;
	}
	else
	{
		dst->sas_ss_sp = src->sas_ss_sp;
		dst->sas_ss_size = src->sas_ss_size;
		dst->sas_ss_flags = src->sas_ss_flags;
	}
	dst->saved_sigmask = 0;
	dst->restore_sigmask = 0;
	/* Linux: interval timers are not inherited by the child. */
	dst->it_real_expire_ms = 0;
	dst->it_real_interval_ms = 0;
	dst->sighand = NULL;
	/*
	 * Linux CLONE_SIGHAND: one table, later sigaction in either task is
	 * visible to the other. Pending signals and both masks stay above,
	 * on the task. Without the flag the child gets a private copy.
	 */
	if (flags & CLONE_SIGHAND)
	{
		if (!sighand_get(src->sighand))
			return -EINVAL;
		dst->sighand = src->sighand;
		return 0;
	}
	dst->sighand = sighand_alloc_copy(src->sighand);
	if (!dst->sighand)
		return -ENOMEM;
	return 0;
}

void process_sighand_note_ignore(process_t *owner, int sig, int ignored)
{
	process_t *p;
	uint64_t bit;
	uint64_t irq_flags;

	if (!owner || !owner->sighand || !valid_signal(sig))
		return;

	bit = SIGNAL_MASK(sig);
	irq_flags = process_registry_lock();
	for (p = process_list; p; p = p->next)
	{
		if (p->sighand != owner->sighand)
			continue;
		if (ignored)
		{
			p->signal_ignored |= bit;
			p->signal_pending &= ~bit;
		}
		else
			p->signal_ignored &= ~bit;
	}
	process_registry_unlock(irq_flags);
}

int process_rel_init_child(process_t *child, const process_t *parent,
			   pid_t child_pid)
{
	if (!child || !parent)
		return -EINVAL;

	child->task.pid = child_pid;
	child->tgid = child_pid;
	child->ppid = parent->task.pid;
	child->sid = parent->sid;
	child->pgid = parent->pgid;
	process_set_sched_state(child, PROCESS_READY);
	child->mode = parent->mode;
	child->sched_prio = parent->sched_prio;
	child->sched_nice = parent->sched_nice;
	child->personality = parent->personality;
	return 0;
}

int process_mm_cursor_clone(process_t *dst, const process_t *src)
{
	/*
	 * Cursors live only in mm_struct. Child gets a fresh mm in
	 * fork_child_mm_create() which copies heap/stack/mmap_base from parent.
	 */
	if (!dst || !src)
		return -EINVAL;
	dst->mm = NULL;
	return 0;
}

int process_session_attrs_clone(process_t *dst, const process_t *src)
{
	if (!dst || !src)
		return -EINVAL;

	memcpy(dst->cwd, src->cwd, sizeof(dst->cwd));
	memcpy(dst->root, src->root, sizeof(dst->root));
	memcpy(dst->comm, src->comm, sizeof(dst->comm));
	/* A fork runs the parent's image until it execs. */
	memcpy(dst->exe_path, src->exe_path, sizeof(dst->exe_path));
	memcpy(dst->rlimits, src->rlimits, sizeof(dst->rlimits));
	dst->robust_list = src->robust_list;
	if (process_saved_environ_clone(dst, src) != 0)
		return -ENOMEM;
	return process_saved_cmdline_clone(dst, src);
}
