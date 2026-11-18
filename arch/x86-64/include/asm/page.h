/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/*
 * Base of the x86-64 direct map (Linux PAGE_OFFSET, 4-level paging). Zero
 * while the kernel still reaches RAM through its low identity map.
 */
#define IR0_ARCH_PAGE_OFFSET 0x0UL
