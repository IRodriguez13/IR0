/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: atomic.h
 * Description: Atomic counters, bit locks, cmpxchg() and barrier() (Linux
 *              <linux/atomic.h>, <linux/bitops.h>); the ISA backend is
 *              <asm/atomic.h>.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/*
 * The only way portable code reaches atomic instructions. arch-guard
 * [no-raw-atomics] rejects __atomic_*, __sync_* and inline asm outside
 * arch/ and the backend, so a new ISA (or an SMP backend) changes one file.
 *
 * Ordering follows Linux (Documentation/atomic_t.txt):
 *
 *   atomic{,64}_read / _set                   relaxed load / store
 *   atomic{,64}_read_acquire / _set_release   acquire load / release store
 *   atomic{,64}_add / _sub                    relaxed, no return value
 *   atomic{,64}_add_return                    fully ordered, new value
 *   atomic{,64}_fetch_add                     fully ordered, old value
 *   atomic{,64}_cmpxchg                       fully ordered, old value
 *   cmpxchg(ptr, old, new)                    same, on any scalar
 *
 *   test_bit(nr, addr)                        relaxed read of bit @nr
 *   test_and_set_bit_lock(nr, addr)           acquire; old bit (try-lock)
 *   clear_bit_unlock(nr, addr)                release (unlock)
 *
 *   barrier()                                 compiler barrier only; CPU
 *                                             fences are smp_mb() in
 *                                             <ir0/cpu.h>
 */

typedef struct
{
	int counter;
} atomic_t;

typedef struct
{
	int64_t counter;
} atomic64_t;

#define ATOMIC_INIT(i)	{ (i) }

#include <asm/atomic.h>
