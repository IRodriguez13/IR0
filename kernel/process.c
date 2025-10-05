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

// Get process list for ps command
process_t *get_process_list(void) {
    return process_list;
}

// Print all processes (for debugging)
void process_print_all(void) {
    extern void serial_print(const char *str);
    extern void serial_print_hex32(uint32_t num);
    
    serial_print("SERIAL: Process list:\n");
    
    // If process_list is empty but current_process exists, add it
    if (!process_list && current_process) {
        serial_print("SERIAL: Adding current_process to empty list\n");
        process_list = current_process;
        current_process->next = NULL;
    }
    
    process_t *proc = process_list;
    int count = 0;
    
    while (proc && count < 10) {
        serial_print("SERIAL: PID=");
        serial_print_hex32(proc->pid);
        serial_print(" state=");
        serial_print_hex32(proc->state);
        serial_print(" next=");
        serial_print_hex32((uint32_t)(uintptr_t)proc->next);
        serial_print("\n");
        
        proc = proc->next;
        count++;
    }
    
    if (current_process) {
        serial_print("SERIAL: Current process PID=");
        serial_print_hex32(current_process->pid);
        serial_print("\n");
    } else {
        serial_print("SERIAL: No current process\n");
    }
    
    if (!process_list) {
        serial_print("SERIAL: No process list found\n");
    } else {
        serial_print("SERIAL: Process list exists\n");
    }
}

// Show process list in shell format
void show_process_list_in_shell(void) {
    extern int64_t sys_write(int fd, const void *buf, size_t count);
    extern void serial_print(const char *str);
    extern void serial_print_hex32(uint32_t num);
    
    process_t *proc = process_list;
    int count = 0;
    
    while (proc && count < 10) {
        serial_print("SERIAL: Showing process PID=");
        serial_print_hex32(proc->pid);
        serial_print("\n");
        
        // Show in shell
        sys_write(1, "  ", 2);
        
        // Convert PID to string
        char pid_str[12];
        uint32_t pid = proc->pid;
        int len = 0;
        if (pid == 0) {
            pid_str[len++] = '0';
        } else {
            char temp[12];
            int temp_len = 0;
            while (pid > 0) {
                temp[temp_len++] = '0' + (pid % 10);
                pid /= 10;
            }
            for (int i = temp_len - 1; i >= 0; i--) {
                pid_str[len++] = temp[i];
            }
        }
        pid_str[len] = '\0';
        
        sys_write(1, pid_str, len);
        
        // Show state and command
        switch (proc->state) {
            case 0: sys_write(1, "  READY   ", 10); break;
            case 1: sys_write(1, "  RUNNING ", 10); break;
            case 2: sys_write(1, "  BLOCKED ", 10); break;
            case 3: sys_write(1, "  SLEEPING", 10); break;
            case 4: sys_write(1, "  ZOMBIE  ", 10); break;
            default: sys_write(1, "  UNKNOWN ", 10); break;
        }
        
        if (proc->pid == 1) {
            sys_write(1, " shell\n", 7);
        } else {
            sys_write(1, " process\n", 9);
        }
        
        proc = proc->next;
        count++;
    }
}

