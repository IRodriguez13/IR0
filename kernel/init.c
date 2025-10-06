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
#include <memory/allocator.h>
#include "scheduler/task.h"
#include <ir0/print.h>
#include <stddef.h>
#include <stdint.h>

// Forward declarations
extern void *kmalloc(size_t size);
extern process_t *current_process;
extern void cfs_add_task_impl(task_t *task);
extern void switch_to_user_mode(void *entry_point);
extern void shell_ring3_entry(void);

void init_1(void) {
  // This is the first user process (PID 1)

  // Launch the shell
  extern void shell_ring3_entry(void);
  shell_ring3_entry();

  // If shell exits, restart it
  for (;;) {
    shell_ring3_entry();
  }
}

// Create and start init process from kernel
int start_init_process(void) {
  // Create process structure
  extern process_t *current_process;
  process_t *init = kmalloc(sizeof(process_t));
  if (!init) {
    return -1;
  }

  // Initialize init process
  process_pid(init) = 1;
  init->state = PROCESS_READY;
  process_rip(init) = (uint64_t)init_1;
  process_rsp(init) = 0x1000000 - 0x1000; // 16MB - 4KB = Safe stack
  process_cs(init) = 0x1B;                // User code (GDT entry 3, RPL=3)
  process_ss(init) = 0x23;                // User data (GDT entry 4, RPL=3)
  process_rflags(init) = 0x202;           // IF=1, Reserved=1
  
  // Create page directory for init process
  extern uint64_t create_process_page_directory(void);
  extern void kfree(void *ptr);
  init->page_directory = (uint64_t*)create_process_page_directory();
  if (!init->page_directory) {
    kfree(init);
    return -1;
  }
  
  // Set up memory layout
  init->heap_start = 0x2000000;  // 32MB
  init->heap_end = 0x2000000;    // Initially empty
  init->stack_start = 0x1000000; // 16MB
  init->stack_size = 0x1000;     // 4KB

  // Add to scheduler
  extern void cfs_add_task_impl(task_t * task);
  cfs_add_task_impl(&init->task);
  current_process = init;

  return 0;
}
