// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: init.c
 * Description: PID 1 init process - init_1 implementation for service management
 * and shell supervision
 */

#include "process.h"
#include <ir0/memory/allocator.h>
#include <ir0/memory/kmem.h>
#include "scheduler/task.h"
#include <ir0/print.h>
#include <stddef.h>
#include <stdint.h>
#include <shell.h>
#include <rr_sched.h>

// Forward declarations
extern process_t *current_process;
extern uint64_t *create_process_page_directory(void);

// init_1: lo que ejecuta el proceso en modo usuario
void init_1(void)
{
  shell_entry();
  for (;;)
    shell_entry();
}

// Create and start init process from kernel
int start_init_process(void)
{
  process_t *init = kmalloc(sizeof(process_t));
  if (!init)
    return -1;

  process_pid(init) = 1;
  init->state = PROCESS_READY;
  init->ppid = 0;
  init->parent = NULL;
  init->children = NULL;
  init->sibling = NULL;
  init->exit_code = 0;

  process_rip(init) = (uint64_t)init_1;
  init->stack_start = 0x1000000;
  init->stack_size = 0x1000;
  process_rsp(init) = init->stack_start + init->stack_size - 8;
  process_rbp(init) = process_rsp(init);

  process_cs(init) = 0x1B;
  process_ss(init) = 0x23;
  process_rflags(init) = 0x202;

  init->page_directory = (uint64_t *)create_process_page_directory();
  if (!init->page_directory)
  {
    kfree(init);
    return -1;
  }

  init->task.rip = process_rip(init);
  init->task.rsp = process_rsp(init);
  init->task.rbp = process_rbp(init);
  init->task.cs = process_cs(init);
  init->task.ss = process_ss(init);
  init->task.rflags = process_rflags(init);

  rr_add_process(init);

  return 0;
}
