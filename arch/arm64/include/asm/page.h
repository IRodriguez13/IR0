/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/*
 * Base of the ARM64 linear map, as in Linux with 48-bit VAs: the bottom of
 * the TTBR1 (kernel) half.
 */
#define IR0_ARCH_PAGE_OFFSET 0xffff000000000000UL
