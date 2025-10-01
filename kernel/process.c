// ===============================================================================
// PROCESS MANAGEMENT - MINIMAL
// ===============================================================================

#include "process.h"
#include <ir0/print.h>

// Global variables
process_t *current_process = NULL;
process_t *idle_process = NULL;

// Initialize process subsystem
void process_init(void)
{
    current_process = NULL;
    idle_process = NULL;
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
        current_process->state = PROCESS_TERMINATED;
    }
    for(;;) __asm__ volatile("hlt");
}

// Stub functions for compatibility
process_t *get_process_list(void) { return NULL; }
process_t *process_create(void (*entry)(void *), void *arg, uint8_t priority, int8_t nice) 
{ 
    (void)entry; (void)arg; (void)priority; (void)nice;
    return NULL; 
}
void process_destroy(process_t *process) { (void)process; }
int process_wait(pid_t pid, int *status) { (void)pid; (void)status; return -1; }
void process_wakeup(process_t *process) { (void)process; }
void process_switch(process_t *from, process_t *to) { (void)from; (void)to; }
void process_save_context(process_t *process) { (void)process; }
void process_restore_context(process_t *process) { (void)process; }
process_t *process_find_by_pid(pid_t pid) { (void)pid; return NULL; }
void process_send_signal(pid_t pid, int signal) { (void)pid; (void)signal; }
void process_handle_signals(process_t *process) { (void)process; }
void process_print_info(process_t *process) { (void)process; }
void process_print_all(void) { }
