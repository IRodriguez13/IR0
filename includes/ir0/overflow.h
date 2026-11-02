/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

/*
 * Typed arithmetic checks for portable kernel code.  Keep policy at the call
 * site (errno, saturation, retry); this helper only answers whether the exact
 * operation fits in the destination type.  GCC and Clang provide this in
 * freestanding mode for every integer type used by IR0.
 */
#define ir0_add_overflow(a, b, result) __builtin_add_overflow((a), (b), (result))

