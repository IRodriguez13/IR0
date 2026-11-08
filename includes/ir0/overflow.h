/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <ir0/types.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Typed arithmetic checks for portable kernel code.  Keep policy at the call
 * site (errno, saturation, retry); this helper only answers whether the exact
 * operation fits in the destination type.  GCC and Clang provide this in
 * freestanding mode for every integer type used by IR0.
 */
#define ir0_add_overflow(a, b, result) __builtin_add_overflow((a), (b), (result))
#define ir0_mul_overflow(a, b, result) __builtin_mul_overflow((a), (b), (result))

static inline int ir0_off_add_size_overflow(off_t base, size_t delta,
                                             off_t *result)
{
    if (!result || delta > (size_t)INT64_MAX)
        return 1;
    return ir0_add_overflow(base, (off_t)delta, result);
}
