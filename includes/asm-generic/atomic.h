/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: atomic.h
 * Description: Generic <ir0/atomic.h> backend over the compiler __atomic
 *              builtins (Linux asm-generic/atomic.h). An ISA selects it from
 *              its asm/atomic.h unless it needs its own instructions.
 *              Include <ir0/atomic.h>, which defines the types used here.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#define barrier()	__asm__ __volatile__("" ::: "memory")

/*
 * One definition per operation, instantiated for atomic_t and atomic64_t.
 * __ATOMIC_SEQ_CST is the "fully ordered" of Linux atomic_t.txt.
 */
#define IR0_ATOMIC_OPS(pfx, atype, ctype)				\
static inline ctype pfx##_read(const atype *v)				\
{									\
	return __atomic_load_n(&v->counter, __ATOMIC_RELAXED);		\
}									\
static inline void pfx##_set(atype *v, ctype i)				\
{									\
	__atomic_store_n(&v->counter, i, __ATOMIC_RELAXED);		\
}									\
static inline ctype pfx##_read_acquire(const atype *v)			\
{									\
	return __atomic_load_n(&v->counter, __ATOMIC_ACQUIRE);		\
}									\
static inline void pfx##_set_release(atype *v, ctype i)			\
{									\
	__atomic_store_n(&v->counter, i, __ATOMIC_RELEASE);		\
}									\
static inline void pfx##_add(ctype i, atype *v)				\
{									\
	(void)__atomic_fetch_add(&v->counter, i, __ATOMIC_RELAXED);	\
}									\
static inline void pfx##_sub(ctype i, atype *v)				\
{									\
	(void)__atomic_fetch_sub(&v->counter, i, __ATOMIC_RELAXED);	\
}									\
static inline ctype pfx##_add_return(ctype i, atype *v)			\
{									\
	return __atomic_add_fetch(&v->counter, i, __ATOMIC_SEQ_CST);	\
}									\
static inline ctype pfx##_fetch_add(ctype i, atype *v)			\
{									\
	return __atomic_fetch_add(&v->counter, i, __ATOMIC_SEQ_CST);	\
}									\
static inline ctype pfx##_cmpxchg(atype *v, ctype old, ctype new_value)	\
{									\
	__atomic_compare_exchange_n(&v->counter, &old, new_value, 0,	\
				    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);\
	return old;							\
}

IR0_ATOMIC_OPS(atomic, atomic_t, int)
IR0_ATOMIC_OPS(atomic64, atomic64_t, int64_t)

#undef IR0_ATOMIC_OPS

/* Returns the value *ptr held before the call; stores @n only if it was @o. */
#define cmpxchg(ptr, o, n)						\
({									\
	__typeof__(*(ptr)) __old = (o);					\
	__atomic_compare_exchange_n((ptr), &__old, (n), 0,		\
				    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);\
	__old;								\
})

#define IR0_BITS_PER_LONG	(8 * sizeof(unsigned long))
#define IR0_BIT_WORD(nr)	((nr) / IR0_BITS_PER_LONG)
#define IR0_BIT_MASK(nr)	(1UL << ((nr) % IR0_BITS_PER_LONG))

static inline int test_bit(unsigned long nr, const unsigned long *addr)
{
	return (__atomic_load_n(&addr[IR0_BIT_WORD(nr)], __ATOMIC_RELAXED) &
		IR0_BIT_MASK(nr)) != 0;
}

static inline int test_and_set_bit_lock(unsigned long nr,
					unsigned long *addr)
{
	unsigned long mask = IR0_BIT_MASK(nr);

	return (__atomic_fetch_or(&addr[IR0_BIT_WORD(nr)], mask,
				  __ATOMIC_ACQUIRE) & mask) != 0;
}

static inline void clear_bit_unlock(unsigned long nr, unsigned long *addr)
{
	(void)__atomic_fetch_and(&addr[IR0_BIT_WORD(nr)],
				 ~IR0_BIT_MASK(nr), __ATOMIC_RELEASE);
}
