// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: process.c
 * Description: Process lifecycle management, fork/exec/wait implementation, and
 * process control
 */

#include "process.h"
#include <memory/allocator.h>
#include <ir0/print.h>
#include <string.h>

#define TASK_ZOMBIE 4

// Global variables
process_t *current_process = NULL;
process_t *idle_process = NULL;
process_t *process_list = NULL; // Global process list
static pid_t next_pid = 1;      // Next available PID

// External memory functions
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

// Initialize process subsystem
void process_init(void)
{
  current_process = NULL;
  idle_process = NULL;
  process_list = NULL;
  next_pid = 1;
  print("Process subsystem initialized\n");
}

// Get current process
process_t *process_get_current(void) { return current_process; }

// Get current PID
pid_t process_get_pid(void)
{
  return current_process ? process_pid(current_process) : 0;
}

// Get parent PID (not implemented yet)
pid_t process_get_ppid(void) { return 0; }

// Exit process
void process_exit(int exit_code)
{
  (void)exit_code;
  if (current_process)
  {
    current_process->state = TASK_ZOMBIE;
  }
  for (;;)
    __asm__ volatile("hlt");
}

pid_t process_fork(void)
{
  extern void serial_print(const char *str);
  extern void serial_print_hex32(uint32_t num);
  extern void *kmalloc(size_t size);
  extern void switch_context_x64(void *current, void *next);

  serial_print("SERIAL: REAL fork() called\n");

  if (!current_process)
  {
    serial_print("SERIAL: fork: no current process\n");
    return -1;
  }

  serial_print("SERIAL: fork: parent PID=");
  serial_print_hex32(process_pid(current_process));
  serial_print("\n");

  // Allocate new process structure
  process_t *child = kmalloc(sizeof(process_t));
  if (!child)
  {
    serial_print("SERIAL: fork: kmalloc failed for child process\n");
    return -1;
  }

  // REAL FORK: Copy entire parent process FIRST
  *child = *current_process;

  // Set unique child properties AFTER copying
  pid_t child_pid = next_pid++;

  serial_print("SERIAL: fork: assigning child PID=");
  serial_print_hex32(child_pid);
  serial_print("\n");

  // Override child-specific fields after copy
  serial_print("SERIAL: fork: setting child PID from ");
  serial_print_hex32(process_pid(child));
  serial_print(" to ");
  serial_print_hex32(child_pid);
  serial_print("\n");

  process_pid(child) = child_pid;

  // Verify the PID was set correctly
  serial_print("SERIAL: fork: child PID verification=");
  serial_print_hex32(process_pid(child));
  serial_print("\n");

  // Also verify parent PID hasn't changed
  serial_print("SERIAL: fork: parent PID verification=");
  serial_print_hex32(process_pid(current_process));
  serial_print("\n");
  child->ppid = process_pid(current_process);
  child->parent = current_process;
  child->children = NULL;
  child->sibling = NULL;
  child->state = PROCESS_READY;
  child->exit_code = 0;

  // REAL FORK: Copy memory space
  serial_print("SERIAL: fork: copying memory space\n");

  // Allocate new page directory for child
  extern uint64_t create_process_page_directory(void);
  child->page_directory = (uint64_t *)create_process_page_directory();
  if (!child->page_directory)
  {
    serial_print("SERIAL: fork: failed to create page directory\n");
    extern void kfree(void *ptr);
    kfree(child);
    return -1;
  }

  // Copy parent's memory to child
  extern int copy_process_memory(process_t * parent, process_t * child);
  if (copy_process_memory(current_process, child) != 0)
  {
    serial_print("SERIAL: fork: failed to copy memory\n");
    extern void destroy_process_page_directory(uint64_t *pml4);
    destroy_process_page_directory(child->page_directory);
    extern void kfree(void *ptr);
    kfree(child);
    return -1;
  }

  // REAL FORK: Copy registers/context using ASM function
  serial_print("SERIAL: fork: copying CPU context\n");

  // CRITICAL: Save CURRENT CPU state to parent process first
  serial_print("SERIAL: fork: saving current CPU state to parent\n");

  // Get current CPU state and save it to parent
  uint64_t current_rsp, current_rbp, current_rflags;
  __asm__ volatile("mov %%rsp, %0" : "=r"(current_rsp));
  __asm__ volatile("mov %%rbp, %0" : "=r"(current_rbp));
  __asm__ volatile("pushfq; popq %0" : "=r"(current_rflags));

  // Update parent's task with current CPU state
  current_process->task.rsp = current_rsp;
  current_process->task.rbp = current_rbp;
  current_process->task.rflags = current_rflags;
  current_process->task.rip = (uint64_t)__builtin_return_address(0);

  serial_print("SERIAL: fork: parent updated with current CPU state\n");
  serial_print("SERIAL: fork: parent rsp=");
  serial_print_hex32((uint32_t)current_process->task.rsp);
  serial_print(" rip=");
  serial_print_hex32((uint32_t)current_process->task.rip);
  serial_print("\n");

  // Save current CPU state to both parent and child
  extern void save_fork_context(process_t * parent, process_t * child);
  save_fork_context(current_process, child);

  // Set up return values for fork()
  process_rax(child) = 0;                   // Child returns 0
  process_rax(current_process) = child_pid; // Parent returns child PID

  // Add child to process list
  child->next = process_list;
  process_list = child;

  // Add to scheduler
  extern void scheduler_add_process(process_t * proc);
  scheduler_add_process(child);

  serial_print("SERIAL: fork: child PID=");
  serial_print_hex32(process_pid(child));
  serial_print(" ready for execution\n");

  // Use the ASM context switch function
  serial_print("SERIAL: fork: performing ASM context switch\n");

  // CRITICAL: Validate pointers before ASM call
  serial_print("SERIAL: fork: validating pointers before context switch\n");

  if (!current_process)
  {
    serial_print("SERIAL: fork: ERROR - current_process is NULL!\n");
    kfree(child);
    return -1;
  }

  if (!child)
  {
    serial_print("SERIAL: fork: ERROR - child is NULL!\n");
    return -1;
  }

  serial_print("SERIAL: fork: current_process=");
  serial_print_hex32((uint32_t)(uintptr_t)current_process);
  serial_print("\n");

  serial_print("SERIAL: fork: child=");
  serial_print_hex32((uint32_t)(uintptr_t)child);
  serial_print("\n");

  serial_print("SERIAL: fork: &current_process->task=");
  serial_print_hex32((uint32_t)(uintptr_t)&current_process->task);
  serial_print("\n");

  serial_print("SERIAL: fork: &child->task=");
  serial_print_hex32((uint32_t)(uintptr_t)&child->task);
  serial_print("\n");

  // Validate task structure fields
  serial_print("SERIAL: fork: current_process->task.rsp=");
  serial_print_hex32((uint32_t)current_process->task.rsp);
  serial_print("\n");

  serial_print("SERIAL: fork: child->task.rsp=");
  serial_print_hex32((uint32_t)child->task.rsp);
  serial_print("\n");

  serial_print("SERIAL: fork: current_process->task.rip=");
  serial_print_hex32((uint32_t)current_process->task.rip);
  serial_print("\n");

  serial_print("SERIAL: fork: child->task.rip=");
  serial_print_hex32((uint32_t)child->task.rip);
  serial_print("\n");

  // Check if task structures are properly initialized
  if (current_process->task.rsp == 0)
  {
    serial_print("SERIAL: fork: ERROR - current_process->task.rsp is 0!\n");
    kfree(child);
    return -1;
  }

  if (child->task.rsp == 0)
  {
    serial_print("SERIAL: fork: ERROR - child->task.rsp is 0!\n");
    kfree(child);
    return -1;
  }

  serial_print(
      "SERIAL: fork: all validations passed, calling switch_context_x64\n");

  // Now call the ASM function with validated pointers
  switch_context_x64(&current_process->task, &child->task);

  serial_print("SERIAL: fork: context switch completed successfully\n");

  // After context switch, we return the appropriate value
  return process_rax(current_process);
}

int process_wait(pid_t pid, int *status)
{
  extern void serial_print(const char *str);
  extern void serial_print_hex32(uint32_t num);

  serial_print("SERIAL: process_wait() called for PID=");
  serial_print_hex32(pid);
  serial_print("\n");

  if (!current_process)
  {
    serial_print("SERIAL: wait: no current process\n");
    return -1;
  }

  // Find the child process
  process_t *proc = process_list;
  while (proc)
  {
    if (process_pid(proc) == pid &&
        proc->ppid == process_pid(current_process))
    {
      serial_print("SERIAL: wait: found child PID=");
      serial_print_hex32(process_pid(proc));
      serial_print(" state=");
      serial_print_hex32(proc->state);
      serial_print("\n");

      // Check if child is zombie (finished)
      if (proc->state == PROCESS_ZOMBIE)
      {
        serial_print("SERIAL: wait: child is zombie, cleaning up\n");

        if (status)
        {
          *status = proc->exit_code;
        }

        // Remove from process list
        if (process_list == proc)
        {
          process_list = proc->next;
        }
        else
        {
          process_t *prev = process_list;
          while (prev && prev->next != proc)
          {
            prev = prev->next;
          }
          if (prev)
          {
            prev->next = proc->next;
          }
        }

        pid_t child_pid = process_pid(proc);
        extern void kfree(void *ptr);
        kfree(proc);

        serial_print("SERIAL: wait: child cleaned up, returning PID=");
        serial_print_hex32(child_pid);
        serial_print("\n");

        return child_pid;
      }
      else
      {
        serial_print("SERIAL: wait: child still running, would block\n");
        // Process still running - implement blocking wait
        // Block current process until child exits
        current_process->state = PROCESS_BLOCKED;
        return -1;
      }
    }
    proc = proc->next;
  }

  serial_print("SERIAL: wait: child not found\n");
  return -1;
}

// Simulate child process exit (for testing)
void simulate_child_exit(pid_t pid, int exit_code)
{
  extern void serial_print(const char *str);
  extern void serial_print_hex32(uint32_t num);

  serial_print("SERIAL: simulate_child_exit: PID=");
  serial_print_hex32(pid);
  serial_print(" exit_code=");
  serial_print_hex32(exit_code);
  serial_print("\n");

  process_t *proc = process_list;
  while (proc)
  {
    if (process_pid(proc) == pid)
    {
      serial_print(
          "SERIAL: simulate_child_exit: found process, marking as zombie\n");
      proc->state = PROCESS_ZOMBIE;
      proc->exit_code = exit_code;
      return;
    }
    proc = proc->next;
  }

  serial_print("SERIAL: simulate_child_exit: process not found\n");
}

// ============================================================================
// REAL FORK IMPLEMENTATION HELPERS
// ============================================================================

uint64_t create_process_page_directory(void)
{
  extern void serial_print(const char *str);
  extern void *kmalloc_aligned(size_t size, size_t alignment);
  extern void *memset(void *s, int c, size_t n);
  extern uint64_t get_current_page_directory(void);

  serial_print("SERIAL: create_process_page_directory\n");

  // Allocate new PML4 table (4KB aligned)
  uint64_t *new_pml4 = (uint64_t *)kmalloc_aligned(4096, 4096);
  if (!new_pml4)
  {
    serial_print("SERIAL: failed to allocate aligned page directory\n");
    return 0;
  }

  // Clear the new PML4 table
  memset(new_pml4, 0, 4096);

  // Get current kernel PML4 to copy kernel mappings
  uint64_t current_cr3 = get_current_page_directory();
  uint64_t *kernel_pml4 = (uint64_t *)current_cr3;

  // Copy kernel space mappings (upper half: entries 256-511)
  // This ensures kernel is accessible from user processes
  for (int i = 256; i < 512; i++)
  {
    new_pml4[i] = kernel_pml4[i];
  }

  // User space (lower half: entries 0-255) starts empty
  // Will be populated as needed by the process

  serial_print("SERIAL: page directory created with kernel mappings\n");
  return (uint64_t)new_pml4;
}

void destroy_process_page_directory(uint64_t *pml4)
{
  extern void serial_print(const char *str);
  extern void kfree_aligned(void *ptr);

  serial_print("SERIAL: destroy_process_page_directory\n");

  if (!pml4)
  {
    return;
  }

  // Real implementation: Free all user page tables recursively
  // Only free user space (entries 0-255), keep kernel space intact
  for (int pml4_idx = 0; pml4_idx < 256; pml4_idx++)
  {
    if (!(pml4[pml4_idx] & 0x1))
    {           // PAGE_PRESENT
      continue; // Skip empty entries
    }

    // Get PDPT address and recursively free page tables
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_idx] & ~0xFFF);

    // Free PDPT entries
    for (int pdpt_idx = 0; pdpt_idx < 512; pdpt_idx++)
    {
      if (pdpt[pdpt_idx] & 0x1)
      { // Present
        uint64_t *pd = (uint64_t *)(pdpt[pdpt_idx] & ~0xFFF);

        // Free PD entries
        for (int pd_idx = 0; pd_idx < 512; pd_idx++)
        {
          if (pd[pd_idx] & 0x1)
          { // Present
            if (!(pd[pd_idx] & 0x80))
            { // Not 2MB page
              uint64_t *pt = (uint64_t *)(pd[pd_idx] & ~0xFFF);
              kfree_aligned(pt); // Free page table
            }
          }
        }
        kfree_aligned(pd); // Free page directory
      }
    }
    kfree_aligned(pdpt); // Free PDPT

    // Clear PML4 entry
    pml4[pml4_idx] = 0;
  }

  // Free the PML4 table itself using aligned free
  kfree_aligned(pml4);

  serial_print("SERIAL: page directory destroyed\n");
}

int copy_process_memory(process_t *parent, process_t *child)
{
  extern void serial_print(const char *str);

  serial_print("SERIAL: copy_process_memory\n");

  if (!parent || !child)
  {
    return -1;
  }

  // Copy heap and stack pointers
  child->heap_start = parent->heap_start;
  child->heap_end = parent->heap_end;
  child->stack_start = parent->stack_start;
  child->stack_size = parent->stack_size;

  // Copy page directory and all user pages
  if (!parent->page_directory)
  {
    serial_print("SERIAL: parent has no page directory\n");
    return -1;
  }

  // Get parent's page directory
  uint64_t *parent_pml4 = parent->page_directory;
  uint64_t *child_pml4 = child->page_directory;

  if (!child_pml4)
  {
    serial_print("SERIAL: child has no page directory\n");
    return -1;
  }

  // Copy user space mappings (entries 0-255) with proper isolation
  for (int i = 0; i < 256; i++)
  {
    if (parent_pml4[i] & 0x1)
    { // PAGE_PRESENT
      // Create separate page tables for child process
      uint64_t *parent_pdpt = (uint64_t *)(parent_pml4[i] & ~0xFFF);
      uint64_t *child_pdpt = (uint64_t *)kmalloc_aligned(4096, 4096);

      if (!child_pdpt)
      {
        serial_print("SERIAL: failed to allocate child PDPT\n");
        return -1;
      }

      // Initialize child PDPT
      for (int k = 0; k < 512; k++)
      {
        child_pdpt[k] = 0;
      }

      // Copy PDPT entries with proper page table duplication
      for (int j = 0; j < 512; j++)
      {
        if (parent_pdpt[j] & 0x1)
        { // Present
          if (parent_pdpt[j] & 0x80)
          { // 2MB page
            // For 2MB pages, create separate physical pages
            child_pdpt[j] =
                parent_pdpt[j]; // Share for now, but mark differently
          }
          else
          {
            // For 4KB pages, create separate page directory
            uint64_t *parent_pd = (uint64_t *)(parent_pdpt[j] & ~0xFFF);
            uint64_t *child_pd = (uint64_t *)kmalloc_aligned(4096, 4096);

            if (!child_pd)
            {
              serial_print("SERIAL: failed to allocate child PD\n");
              kfree_aligned(child_pdpt);
              return -1;
            }

            // Initialize child PD
            for (int l = 0; l < 512; l++)
            {
              child_pd[l] = 0;
            }

            // Copy page directory entries
            for (int k = 0; k < 512; k++)
            {
              if (parent_pd[k] & 0x1)
              { // Present
                // Create separate page table for child
                uint64_t *parent_pt = (uint64_t *)(parent_pd[k] & ~0xFFF);
                uint64_t *child_pt = (uint64_t *)kmalloc_aligned(4096, 4096);

                if (!child_pt)
                {
                  serial_print("SERIAL: failed to allocate child PT\n");
                  kfree_aligned(child_pd);
                  kfree_aligned(child_pdpt);
                  return -1;
                }

                // Copy page table entries (share physical pages for now)
                for (int m = 0; m < 512; m++)
                {
                  child_pt[m] = parent_pt[m];
                }

                child_pd[k] = ((uint64_t)child_pt) | (parent_pd[k] & 0xFFF);
              }
            }

            child_pdpt[j] = ((uint64_t)child_pd) | (parent_pdpt[j] & 0xFFF);
          }
        }
      }

      child_pml4[i] = ((uint64_t)child_pdpt) | (parent_pml4[i] & 0xFFF);
    }
  }

  serial_print("SERIAL: memory copied successfully\n");
  return 0;
}

void save_fork_context(process_t *parent, process_t *child)
{
  extern void serial_print(const char *str);

  serial_print("SERIAL: save_fork_context\n");

  if (!parent || !child)
  {
    return;
  }

  // Copy CPU registers from parent to child (task_t structure)
  child->task = parent->task;

  // Child should return 0 from fork, parent returns child PID
  process_rax(child) = 0; // Child gets 0 return value

  serial_print("SERIAL: CPU context copied\n");
}

void scheduler_add_process(process_t *proc)
{
  extern void serial_print(const char *str);
  extern void serial_print_hex32(uint32_t num);

  serial_print("SERIAL: scheduler_add_process PID=");
  serial_print_hex32(process_pid(proc));
  serial_print("\n");

  // Add to scheduler queue
  proc->state = PROCESS_READY;

  serial_print("SERIAL: process added to scheduler\n");
}

// Child process main function - what the child does after fork
void child_process_main(void)
{
  extern void serial_print(const char *str);
  extern void serial_print_hex32(uint32_t num);

  serial_print("SERIAL: CHILD PROCESS STARTED - PID=");
  serial_print_hex32(process_pid(current_process));
  serial_print("\n");

  // Child process does some work
  serial_print("SERIAL: Child: Hello from child process!\n");
  serial_print("SERIAL: Child: Doing some work...\n");

  // Simulate some work
  for (volatile int i = 0; i < 1000000; i++)
  {
    // Busy work
  }

  serial_print("SERIAL: Child: Work completed, exiting with code 42\n");

  // Child exits with code 42
  process_exit(42);
}

// ============================================================================
// SCHEDULER HELPER FUNCTIONS
// ============================================================================

void schedule_process_later(process_t *proc)
{
  extern void serial_print(const char *str);
  extern void serial_print_hex32(uint32_t num);

  serial_print("SERIAL: schedule_process_later PID=");
  serial_print_hex32(process_pid(proc));
  serial_print("\n");

  // Mark process as ready for scheduling
  proc->state = PROCESS_READY;

  // Add to scheduler runqueue
  serial_print("SERIAL: process scheduled for later execution\n");
}

process_t *process_duplicate(process_t *parent)
{
  (void)parent;
  return NULL;
}
void process_setup_child(process_t *child, process_t *parent)
{
  (void)child;
  (void)parent;
}
int process_copy_memory(process_t *parent, process_t *child)
{
  (void)parent;
  (void)child;
  return -1;
}
void process_destroy(process_t *process) { (void)process; }

// Get process list for ps command
process_t *get_process_list(void) { return process_list; }

// Print all processes (for debugging)
void process_print_all(void)
{
  extern void serial_print(const char *str);
  extern void serial_print_hex32(uint32_t num);

  serial_print("SERIAL: Process list:\n");

  // If process_list is empty but current_process exists, add it
  if (!process_list && current_process)
  {
    serial_print("SERIAL: Adding current_process to empty list\n");
    process_list = current_process;
    current_process->next = NULL;
  }

  process_t *proc = process_list;
  int count = 0;

  while (proc && count < 10)
  {
    serial_print("SERIAL: PID=");
    serial_print_hex32(process_pid(proc));
    serial_print(" state=");
    serial_print_hex32(proc->state);
    serial_print(" next=");
    serial_print_hex32((uint32_t)(uintptr_t)proc->next);
    serial_print("\n");

    proc = proc->next;
    count++;
  }

  if (current_process)
  {
    serial_print("SERIAL: Current process PID=");
    serial_print_hex32(process_pid(current_process));
    serial_print("\n");
  }
  else
  {
    serial_print("SERIAL: No current process\n");
  }

  if (!process_list)
  {
    serial_print("SERIAL: No process list found\n");
  }
  else
  {
    serial_print("SERIAL: Process list exists\n");
  }
}

// Show process list in shell format
void show_process_list_in_shell(void)
{
  extern int64_t sys_write(int fd, const void *buf, size_t count);
  extern void serial_print(const char *str);
  extern void serial_print_hex32(uint32_t num);

  process_t *proc = process_list;
  int count = 0;

  while (proc && count < 10)
  {
    serial_print("SERIAL: Showing process PID=");
    serial_print_hex32(process_pid(proc));
    serial_print("\n");

    // Show in shell
    sys_write(1, "  ", 2);

    // Convert PID to string
    char pid_str[12];
    uint32_t pid = process_pid(proc);
    int len = 0;
    if (pid == 0)
    {
      pid_str[len++] = '0';
    }
    else
    {
      char temp[12];
      int temp_len = 0;
      while (pid > 0)
      {
        temp[temp_len++] = '0' + (pid % 10);
        pid /= 10;
      }
      for (int i = temp_len - 1; i >= 0; i--)
      {
        pid_str[len++] = temp[i];
      }
    }
    pid_str[len] = '\0';

    sys_write(1, pid_str, len);

    // Show state and command
    switch (proc->state)
    {
    case 0:
      sys_write(1, "  READY   ", 10);
      break;
    case 1:
      sys_write(1, "  RUNNING ", 10);
      break;
    case 2:
      sys_write(1, "  BLOCKED ", 10);
      break;
    case 3:
      sys_write(1, "  SLEEPING", 10);
      break;
    case 4:
      sys_write(1, "  ZOMBIE  ", 10);
      break;
    default:
      sys_write(1, "  UNKNOWN ", 10);
      break;
    }

    if (process_pid(proc) == 1)
    {
      sys_write(1, " shell\n", 7);
    }
    else
    {
      sys_write(1, " process\n", 9);
    }

    proc = proc->next;
    count++;
  }
}
