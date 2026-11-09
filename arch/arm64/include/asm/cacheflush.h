/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/*
 * ARM64 instruction caches are not coherent with data stores: code the
 * kernel writes needs clean to PoU + I-cache invalidate (Arm ARM B2.4.4).
 */
#define IR0_ARCH_ICACHE_NEEDS_SYNC 1
