/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/*
 * ARM64 needs no atomics of its own: the compiler builtins emit LDAXR/STLXR
 * exclusive loops (or LSE atomics when the target enables them) and
 * LDAR/STLR for acquire/release (Arm ARM B2.3). The generic backend is the
 * ARM64 implementation.
 */
#include <asm-generic/atomic.h>
