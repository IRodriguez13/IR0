/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/*
 * Base of the x86-64 direct map, as in Linux with 4-level paging: PML4 slot
 * 273 of the kernel half, shared by every process root.
 */
#define IR0_ARCH_PAGE_OFFSET 0xffff888000000000UL
