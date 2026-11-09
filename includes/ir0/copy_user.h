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
 * copy_*_user_mm for a non-current mm), after access_ok() when a pointer
 * is checked ahead of the copy. Kernel threads (dbgshell/ktest) pass kernel
 * buffers through the same calls.
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
 * strncpy_from_user - Copy a NUL-terminated string from userspace (Linux).
 * @dst: kernel destination, at least @count bytes
 * @src: userspace source
 * @count: maximum number of bytes to copy, including the NUL
 *
 * Never reads past the page holding the terminator.
 * Returns: the string length (without the NUL) on success; @count when no
 * NUL was found in @count bytes (@dst is then not terminated); -EFAULT on
 * a bad or unmapped source.
 */
long strncpy_from_user(char *dst, const char *src, long count);

/**
 * access_ok - Check a syscall pointer argument before using it
 * @addr: userspace pointer
 * @size: bytes the syscall will access at @addr
 *
 * For a user process, [@addr, @addr + @size) must lie in the ISA user VA
 * window; mapping and permissions are checked by the copy itself. Kernel
 * threads (KERNEL_MODE: dbgshell, ktest) pass kernel buffers to syscalls and
 * always pass, as copy_{to,from}_user() accept them.
 * Address ranges that are not copied through (mmap hints, signal handlers,
 * TLS bases) use mm_user_va_ok() from <ir0/mm.h>.
 * Returns: 1 if valid, 0 otherwise.
 */
int access_ok(const void *addr, size_t size);

/**
 * access_ok_mapped - access_ok() plus every page present in the current mm
 *
 * For a syscall that consumes data before it copies it out (pipe, socket
 * and file reads), so a bad destination fails before anything is lost.
 * Returns: 1 if valid, 0 otherwise.
 */
int access_ok_mapped(const void *addr, size_t size);

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
 * They walk @root; only user-accessible leaves inside the ISA user VA window
 * are touched, and a write needs a writable leaf or a COW one, which it
 * breaks like a write fault. Fill a mapping before making it read-only.
 * Returns: 0 on success, -EFAULT on error.
 */
int copy_to_user_mm(address_space_root_t root, uintptr_t dst, const void *src,
		    size_t n);
int copy_from_user_mm(address_space_root_t root, uintptr_t src, void *dst,
		      size_t n);
int clear_user_mm(address_space_root_t root, uintptr_t dst, size_t n);

#endif /* _IR0_COPY_USER_H */
