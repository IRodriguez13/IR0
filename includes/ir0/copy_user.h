/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025–2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: copy_user.h
 * Description: Portable kernel↔userspace copy facade (multi-ISA).
 *
 * Contract: never memcpy/memset/strncpy or direct dereference of a userspace
 * pointer. Always copy_to_user / copy_from_user / clear_user (or
 * copy_*_user_mm for a non-current mm). KERNEL_MODE
 * bypass is only for dbgshell/ktest.
 */

#ifndef _IR0_COPY_USER_H
#define _IR0_COPY_USER_H

#include <stddef.h>
#include <stdint.h>
#include <ir0/mm.h>
#include <ir0/errno.h>

/**
 * copy_to_user - Copy data from kernel to user space
 * @dst: Destination address in user space
 * @src: Source address in kernel space
 * @n: Number of bytes to copy
 *
 * Returns: 0 on success, -EFAULT on error
 */
int copy_to_user(void *dst, const void *src, size_t n);

/**
 * copy_from_user - Copy data from user space to kernel
 * @dst: Destination address in kernel space
 * @src: Source address in user space
 * @n: Number of bytes to copy
 *
 * Returns: 0 on success, -EFAULT on error
 */
int copy_from_user(void *dst, const void *src, size_t n);

/**
 * clear_user - Zero @n bytes at userspace @dst (COW-safe via region walk).
 * Returns: 0 on success, -EFAULT on error.
 */
int clear_user(void *dst, size_t n);

/**
 * copy_from_user_cstring - Copy a NUL-terminated string from userspace.
 * Does not read past unmapped pages like a fixed-size copy would when src
 * sits near USER_STACK_TOP. Returns -ENAMETOOLONG instead of accepting a
 * truncated string when the terminator does not fit in @dst.
 */
int copy_from_user_cstring(char *dst, size_t dst_sz, const char *src);

/**
 * is_user_address / access_ok - Canonical userspace VA window (ISA helper).
 * Returns: 1 if valid user address range, 0 otherwise.
 */
int is_user_address(const void *addr, size_t size);
int is_user_address_checked(const void *addr, size_t size, int check_mapped);

static inline int access_ok(const void *addr, size_t size)
{
	return is_user_address(addr, size);
}

/*
 * Scalar helpers (Linux-shaped). Evaluate to 0 on success, -EFAULT on fault.
 * @ptr is a userspace pointer; @x is a kernel lvalue / rvalue.
 */
#define put_user(x, ptr)                                                       \
	({                                                                     \
		__typeof__(*(ptr)) __pu_val = (x);                             \
		copy_to_user((void *)(ptr), &__pu_val, sizeof(__pu_val));      \
	})

#define get_user(x, ptr)                                                       \
	({                                                                     \
		__typeof__(*(ptr)) __gu_tmp;                                   \
		int __gu_ret = copy_from_user(&__gu_tmp, (const void *)(ptr),  \
					      sizeof(__gu_tmp));               \
		if (__gu_ret == 0)                                             \
			(x) = __gu_tmp;                                        \
		__gu_ret;                                                      \
	})

/*
 * Copies against an explicit address space (Linux access_remote_vm analogue)
 * for callers that must reach a process other than current, or a root that
 * is not active yet: ELF load, signal frames, exit/wait bookkeeping.
 * They walk @root and break COW like a write fault; only user-accessible
 * leaves inside the ISA user VA window are touched.
 * Returns: 0 on success, -EFAULT on error.
 */
int copy_to_user_mm(address_space_root_t root, uintptr_t dst, const void *src,
		    size_t n);
int copy_from_user_mm(address_space_root_t root, uintptr_t src, void *dst,
		      size_t n);
int clear_user_mm(address_space_root_t root, uintptr_t dst, size_t n);

#endif /* _IR0_COPY_USER_H */
