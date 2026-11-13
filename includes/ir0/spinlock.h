/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: spinlock.h
 * Description: UP spinlock facade (irq-save); SMP backend later.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/cpu.h>
#include <config.h>

/*
 * UP (default): mask IRQs for the critical section.
 *
 * SMP (future): a per-lock word taken with test_and_set_bit_lock() and
 * released with clear_bit_unlock() from <ir0/atomic.h>; not wired yet.
 */
#if defined(CONFIG_SMP) && CONFIG_SMP
#error "ir0_spinlock: SMP backend not implemented — use UP build for now"
#endif

/*
 * One lock object per protected structure, so an SMP backend has something
 * to spin on. On UP the object carries no state: the IRQ state lives with
 * the holder (Linux spin_lock_irqsave), so the lock nests and works from
 * IRQ context.
 */
typedef struct
{
	unsigned char unused;
} ir0_spinlock_t;

static inline unsigned long ir0_spin_lock_irqsave(ir0_spinlock_t *lock)
{
	(void)lock;
	return irq_save();
}

static inline void ir0_spin_unlock_irqrestore(ir0_spinlock_t *lock,
					      unsigned long flags)
{
	(void)lock;
	irq_restore(flags);
}
