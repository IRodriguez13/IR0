// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: copy_user.h
 * Description: Safe copy functions for kernel<->user data transfer
 */

#ifndef _IR0_COPY_USER_H
#define _IR0_COPY_USER_H

#include <stddef.h>
#include <stdint.h>

/* Error codes */
#define EFAULT 14  /* Bad address */

/**
 * copy_to_user - Copy data from kernel to user space
 * @dst: Destination address in user space
 * @src: Source address in kernel space
 * @n: Number of bytes to copy
 *
 * Returns: 0 on success, -EFAULT on error
 *
 * Validates destination address if current process is in USER_MODE.
 * In KERNEL_MODE (dbgshell), validation is skipped for debugging.
 */
int copy_to_user(void *dst, const void *src, size_t n);

/**
 * copy_from_user - Copy data from user space to kernel
 * @dst: Destination address in kernel space
 * @src: Source address in user space
 * @n: Number of bytes to copy
 *
 * Returns: 0 on success, -EFAULT on error
 *
 * Validates source address if current process is in USER_MODE.
 * In KERNEL_MODE (dbgshell), validation is skipped for debugging.
 */
int copy_from_user(void *dst, const void *src, size_t n);

/**
 * is_user_address - Check if address range is valid for user space
 * @addr: Address to check
 * @size: Size of region
 *
 * Returns: 1 if valid user address, 0 otherwise
 */
int is_user_address(const void *addr, size_t size);

#endif /* _IR0_COPY_USER_H */
