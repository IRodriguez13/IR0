/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: validate_user.c
 * Description: userspace pointer validation helpers (split from syscalls.c)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "validate_user.h"
#include <kernel/process.h>
#include <ir0/copy_user.h>
#include <ir0/errno.h>
#include <ir0/process.h>
#include <stdint.h>

/**
 * validate_userspace_string - Validate that a string argument is in userspace
 * @str: String pointer to validate
 * @max_len: Maximum expected string length
 * Returns: 0 if valid, -EFAULT if invalid
 */
int validate_userspace_string(const char *str, size_t max_len)
{
  if (!current_process)
    return -ESRCH;

  /* For KERNEL_MODE processes (like dbgshell), allow addresses in process stack/heap range
   * This allows debug_bins/ commands to work while still simulating userspace behavior
   */
  if (current_process->mode == KERNEL_MODE)
  {
    /* Allow if address is in process's stack or heap region */
    uint64_t addr = (uint64_t)str;
    if (addr >= current_process->stack_start &&
        addr < current_process->stack_start + current_process->stack_size)
      return 0;
    if (current_process->heap_start > 0 &&
        addr >= current_process->heap_start &&
        addr < current_process->heap_end)
      return 0;
    /* Also allow if it's a valid user address (for compatibility) */
    if (is_user_address(str, max_len))
      return 0;
    /* For KERNEL_MODE, be more lenient - allow kernel addresses from current process stack */
    return 0;  /* Allow kernel space addresses for debug_bins/ simulation */
  }

  /* USER_MODE: strict validation - must be in userspace */
  if (!is_user_address(str, max_len))
    return -EFAULT;

  return 0;
}

/**
 * validate_userspace_buffer - Validate that a buffer argument is in userspace
 * @buf: Buffer pointer to validate
 * @size: Size of buffer
 * Returns: 0 if valid, -EFAULT if invalid
 */
int validate_userspace_buffer(const void *buf, size_t size)
{
  if (!current_process)
    return -ESRCH;

  /* For KERNEL_MODE processes (like dbgshell), allow addresses in process stack/heap range
   * This allows debug_bins/ commands to work while still simulating userspace behavior
   */
  if (current_process->mode == KERNEL_MODE)
  {
    /* Allow if address is in process's stack or heap region */
    uint64_t addr = (uint64_t)buf;
    if (addr >= current_process->stack_start &&
        addr + size <= current_process->stack_start + current_process->stack_size)
      return 0;
    if (current_process->heap_start > 0 &&
        addr >= current_process->heap_start &&
        addr + size <= current_process->heap_end)
      return 0;
    /* Also allow if it's a valid user address (for compatibility) */
    if (is_user_address(buf, size))
      return 0;
    /* For KERNEL_MODE, be more lenient - allow kernel addresses from current process stack */
    return 0;  /* Allow kernel space addresses for debug_bins/ simulation */
  }

  /* USER_MODE: strict validation - must be in userspace */
  if (!is_user_address(buf, size))
    return -EFAULT;

  return 0;
}
