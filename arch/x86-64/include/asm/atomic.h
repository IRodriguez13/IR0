/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/*
 * x86-64 needs no atomics of its own: the compiler builtins emit
 * LOCK-prefixed read-modify-write instructions, and under TSO an acquire
 * load or release store is a plain MOV (SDM vol. 3, 9.2). The generic
 * backend is the x86-64 implementation.
 */
#include <asm-generic/atomic.h>
