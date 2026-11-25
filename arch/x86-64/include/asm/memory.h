/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/*
 * User address-space layout of this ISA. Macros only: config.h, which
 * every C and assembly file includes, reads it.
 */

/* Top of the user stack, below the low identity-mapped kernel window. */
#define IR0_ARCH_USER_STACK_TOP 0x7FFFF000UL

/* A kernel-half address never mapped (test faults): below the direct map. */
#define IR0_ARCH_UNMAPPED_KERNEL_VA 0xFFFF800000000000ULL
