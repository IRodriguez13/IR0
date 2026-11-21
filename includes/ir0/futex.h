/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: futex.h
 * Description: futex wait/wake facade
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

struct process;

/* futex(2) ops and flags (Linux uapi/linux/futex.h). */
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_REQUEUE 3
#define FUTEX_CMP_REQUEUE 4
#define FUTEX_WAIT_BITSET 9
#define FUTEX_WAKE_BITSET 10
#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_CLOCK_REALTIME 256
#define FUTEX_CMD_MASK (~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME))
#define FUTEX_BITSET_MATCH_ANY 0xffffffffU

/* Robust futex word bits. */
#define FUTEX_WAITERS 0x80000000U
#define FUTEX_OWNER_DIED 0x40000000U
#define FUTEX_TID_MASK 0x3fffffffU

/* ir0_futex_* flags: a shared futex (no FUTEX_PRIVATE_FLAG). */
#define IR0_FUTEX_SHARED 1U
/* ir0_futex_wait() deadline that never expires. */
#define IR0_FUTEX_NO_TIMEOUT UINT64_MAX

/*
 * ir0_futex_wait - Sleep on @uaddr while it holds @val (Linux futex_wait()).
 * @proc is current. Ends on a wake whose bitset meets @bitset (0), at the
 * monotonic @deadline_ms (-ETIMEDOUT), or on a signal (-ERESTARTSYS, or
 * -ERESTARTNOHAND for a timed wait). -EAGAIN when the word is not @val,
 * -EINVAL for a zero bitset or a misaligned word, -EFAULT for a bad one.
 */
int ir0_futex_wait(struct process *proc, int *uaddr, int val,
		   unsigned int flags, uint32_t bitset, uint64_t deadline_ms);

/*
 * ir0_futex_wake - Wake up to @nr_wake (at least one) waiters on @uaddr
 * whose bitset meets @bitset, in arrival order. Returns how many woke.
 */
int ir0_futex_wake(struct process *proc, int *uaddr, unsigned int flags,
		   int nr_wake, uint32_t bitset);

/*
 * ir0_futex_requeue - Wake @nr_wake waiters on @uaddr and move up to
 * @nr_requeue more to @uaddr2 (Linux futex_requeue()). With @cmpval, the
 * word must hold *@cmpval or the call is -EAGAIN. Returns woken + moved.
 */
int ir0_futex_requeue(struct process *proc, int *uaddr, int *uaddr2,
		      unsigned int flags, int nr_wake, int nr_requeue,
		      const int *cmpval);

/* Exit: unhook every waiter of @proc. */
void ir0_futex_drop_process(struct process *proc);

/* Exit: release every robust futex @proc holds (kernel/futex/syscalls.c). */
void process_exit_robust_list(struct process *proc);

/*
 * Called right before a waiter sleeps in schedule(), after it found no wake
 * and no pending signal. NULL outside ktest. The hook may mark the task
 * READY or post a signal. It must not call ir0_futex_wait.
 */
extern void (*ir0_futex_before_sleep)(struct process *proc);
