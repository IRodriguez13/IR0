/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/*
 * User address-space layout of this ISA. Macros only: config.h, which
 * every C and assembly file includes, reads it.
 */

/*
 * Top of the user stack. Linux arm64 puts it at TASK_SIZE (1 << 48) with
 * 48-bit VAs; IR0 stops one page short so the stack stays inside
 * mm_user_va_end(). TTBR0 holds only user mappings, so the whole lower
 * half is user address space.
 */
#define IR0_ARCH_USER_STACK_TOP 0x0000FFFFFFFFF000UL

/*
 * A TTBR1 address never mapped (test faults): between the linear map
 * (0xffff000000000000) and the image (KIMAGE_VADDR, 0xffff800000000000).
 */
#define IR0_ARCH_UNMAPPED_KERNEL_VA 0xFFFF700000000000ULL
