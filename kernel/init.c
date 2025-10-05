// init.c - Simple init process (PID 1) for user space
// This is the first user process, like systemd but ultra minimal

#include <stdint.h>
#include <stddef.h>
#include <ir0/print.h>
#include "process.h"
#include "scheduler/task.h"

// Forward declarations
extern void *kmalloc(size_t size);
extern process_t *current_process;
extern void cfs_add_task_impl(task_t *task);
extern void switch_to_user_mode(void *entry_point);
extern void shell_ring3_entry(void);

void init_1(void)
{
  // This is the first user process (PID 1)

  // Launch the shell
  extern void shell_ring3_entry(void);
  shell_ring3_entry();

  // If shell exits, restart it
  for (;;)
  {
    shell_ring3_entry();
  }
}

// Create and start init process from kernel
int start_init_process(void)
{
  // Create process structure
  extern process_t *current_process;
  process_t *init = kmalloc(sizeof(process_t));
  if (!init)
  {
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

  // Add to scheduler
  extern void cfs_add_task_impl(task_t * task);
  cfs_add_task_impl(&init->task);
  current_process = init;

  return 0;
}
