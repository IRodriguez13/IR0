// ===============================================================================
// PROCESS MANAGEMENT - REAL FORK/EXEC SUPPORT
// ===============================================================================

#include "process.h"
#include <ir0/print.h>
#include <string.h>

#define TASK_ZOMBIE 4

// Global variables
process_t *current_process = NULL;
process_t *idle_process = NULL;
process_t *process_list = NULL;  // Global process list
static pid_t next_pid = 1;       // Next available PID

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
process_t *process_get_current(void)
{
    return current_process;
}

// Get current PID
pid_t process_get_pid(void)
{
    return current_process ? current_process->pid : 0;
}

// Get parent PID (not implemented yet)
pid_t process_get_ppid(void)
{
    return 0;
}

// Exit process
void process_exit(int exit_code)
{
    (void)exit_code;
    if (current_process) {
        current_process->state = TASK_ZOMBIE;
    }
    for(;;) __asm__ volatile("hlt");
}


pid_t process_fork(void) {
    // Simplified fork for testing
    return -1;  // Not implemented yet
}

int process_wait(pid_t pid, int *status) {
    // Simplified wait for testing
    (void)pid; (void)status;
    return -1;  // Not implemented yet
}

process_t *process_duplicate(process_t *parent) { (void)parent; return NULL; }
void process_setup_child(process_t *child, process_t *parent) { (void)child; (void)parent; }
int process_copy_memory(process_t *parent, process_t *child) { (void)parent; (void)child; return -1; }
void process_destroy(process_t *process) { (void)process; }

