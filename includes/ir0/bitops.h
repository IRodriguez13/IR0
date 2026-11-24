/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: bitops.h
 * Description: Bitmaps of unsigned long words, Linux include/linux/bitops.h
 *              names. __set_bit and __clear_bit are not atomic: the caller
 *              owns the bitmap (a device being set up, or under its lock).
 *              test_bit comes from <ir0/atomic.h>.
 */

#pragma once

#include <stddef.h>
#include <ir0/atomic.h>

#define BITS_PER_LONG		(8 * (int)sizeof(long))
#define BIT_WORD(nr)		((nr) / BITS_PER_LONG)
#define BIT_MASK(nr)		(1UL << ((nr) % BITS_PER_LONG))
#define BITS_TO_LONGS(nr)	(((nr) + BITS_PER_LONG - 1) / BITS_PER_LONG)

static inline void __set_bit(unsigned long nr, unsigned long *addr)
{
	addr[BIT_WORD(nr)] |= BIT_MASK(nr);
}

static inline void __clear_bit(unsigned long nr, unsigned long *addr)
{
	addr[BIT_WORD(nr)] &= ~BIT_MASK(nr);
}
