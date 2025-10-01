// init.c - Simple init process (PID 1) for user space
// This is the first user process, like systemd but ultra minimal

#include <ir0/print.h>
#include <kernel/process.h>
#include <kernel/scheduler/task.h>
#include <memory/allocator.h>

// Forward declarations
extern void *kmalloc(size_t size);

// Init process entry point (runs in Ring 3)
void init_process_entry(void)
{
    // This code runs in user space (Ring 3)
    // Keep it ULTRA simple - no syscalls that might fault
    
    // Infinite loop - just stay alive
    for (;;) {
        // Busy wait
        for (volatile int i = 0; i < 1000000; i++) {
            // Keep CPU busy
        }
    }
}

// Create and start init process from kernel
int start_init_process(void)
{
    print("Creating init process (PID 1)...\n");
    
    // Create process structure
    extern process_t *current_process;
    process_t *init = kmalloc(sizeof(process_t));
    if (!init) {
        print("ERROR: Cannot allocate init process\n");
        return -1;
    }
    
    // Initialize init process
    init->pid = 1;
    init->state = PROCESS_READY;
    init->priority = 0;
    init->time_slice = 10;
    
    // Set entry point
    init->rip = (uint64_t)init_process_entry;
    
    // Stack WELL within mapped region (use 16MB mark for safety)
    init->rsp = 0x1000000 - 0x1000; // 16MB - 4KB = Safe stack
    
    // User mode segments
    init->cs = 0x1B; // User code (GDT entry 3, RPL=3)
    init->ss = 0x23; // User data (GDT entry 4, RPL=3)
    
    // RFLAGS: Enable interrupts (IF=1) + reserved bit
    init->rflags = 0x202; // IF=1, Reserved=1
    
    // Add to scheduler
    extern void cfs_add_task_impl(task_t *task);
    cfs_add_task_impl((task_t*)init);
    
    current_process = init;
    
    print("Init process created successfully\n");
    print("Switching to user mode...\n");
    
    // Switch to user mode using iretq
    extern void switch_to_user_mode(void *entry_point);
    switch_to_user_mode((void*)init_process_entry);
    
    // Should never return
    return 0;
}
