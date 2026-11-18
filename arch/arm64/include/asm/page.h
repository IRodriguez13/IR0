/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/*
 * Base of the ARM64 linear map (Linux PAGE_OFFSET, 48-bit VA). Zero while
 * the kernel still reaches RAM through its TTBR0 identity map.
 */
#define IR0_ARCH_PAGE_OFFSET 0x0UL
