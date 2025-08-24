// ===============================================================================
// PROCESS MANAGEMENT IMPLEMENTATION
// ===============================================================================

#include "process.h"
#include <print.h>
// #include "../../memory/memo_interface.h"  // Comentado - no existe en esta rama
#include <bump_allocator.h>  // Usar bump_allocator directamente
#include <string.h>

// ===============================================================================
// GLOBAL VARIABLES
// ===============================================================================

process_t *current_process = NULL;
process_t *idle_process = NULL;
uint32_t process_count = 0;
pid_t next_pid = 1;

// Listas de procesos
static process_t *ready_queue = NULL;
static process_t *sleeping_queue = NULL;
static process_t *zombie_queue = NULL;

// ===============================================================================
// HELPER FUNCTIONS
// ===============================================================================

static void process_add_to_list(process_t *process)
{
    if (!process) return;
    
    // Add to ready queue
    process->next = ready_queue;
    if (ready_queue) {
        ready_queue->prev = process;
    }
    ready_queue = process;
    process->prev = NULL;
}

static void process_remove_from_list(process_t *process)
{
    if (!process)
         return;
    
    
    // Remove from current list
    if (process->prev) {
        process->prev->next = process->next;
    } else {
        // Process is at the head of the list
        if (ready_queue == process) {
            ready_queue = process->next;
        } else if (sleeping_queue == process) {
            sleeping_queue = process->next;
        } else if (zombie_queue == process) {
            zombie_queue = process->next;
        }
    }
    
    if (process->next) {
        process->next->prev = process->prev;
    }
    
    process->next = NULL;
    process->prev = NULL;
}

static void process_add_to_zombie_queue(process_t *process)
{
    if (!process) return;
    
    process->next = zombie_queue;
    if (zombie_queue) {
        zombie_queue->prev = process;
    }
    zombie_queue = process;
    process->prev = NULL;
}

static void process_remove_from_zombie_queue(process_t *process)
{
    process_remove_from_list(process);
}

// ===============================================================================
// MAIN PROCESS FUNCTIONS
// ===============================================================================

void process_init(void)
{
    print("Initializing process management system\n");
    
    // Initialize global variables
    current_process = NULL;
    idle_process = NULL;
    process_count = 0;
    next_pid = 1;
    
    // Initialize queues
    ready_queue = NULL;
    sleeping_queue = NULL;
    zombie_queue = NULL;
    
    print_success("Process management system initialized\n");
}

process_t *process_create(const char *name, void (*entry_point)(void *), void *arg)
{
    (void)arg; // Parameter not used yet
    if (!name || !entry_point) {
        return NULL;
    }
    
    // Allocate process structure
    process_t *process = kmalloc(sizeof(process_t));
    if (!process) {
        return NULL;
    }
    
    // Initialize process
    memset(process, 0, sizeof(process_t));
    
    // Set basic information
    process->pid = next_pid++;
    process->ppid = current_process ? current_process->pid : 0;
    process->pgid = process->pid;
    strncpy(process->name, name, MAX_PROCESS_NAME - 1);
    process->name[MAX_PROCESS_NAME - 1] = '\0';
    
    // Set state and priority
    process->state = PROCESS_NEW;
    process->priority = PRIORITY_NORMAL;
    process->flags = PROCESS_FLAG_USER;
    process->exit_code = 0;
    
    // Initialize timing
    process->cpu_time = 0;
    process->start_time = 0; // TODO: Get current time
    process->last_run = 0;
    process->time_slice = 10; // Default time slice
    
    // Initialize memory pointers
    process->page_directory = 0; // TODO: Allocate page directory
    process->kernel_stack = 0;   // TODO: Allocate kernel stack
    process->user_stack = 0;     // TODO: Allocate user stack
    process->heap_start = 0;
    process->heap_end = 0;
    
    // Initialize file descriptors
    for (int i = 0; i < 16; i++) {
        process->open_files[i] = -1;
    }
    
    // Initialize working directory
    process->working_dir = 0; // TODO: Set to root directory
    
    // Initialize signals
    process->signal_mask = 0;
    process->pending_signals = 0;
    
    // Initialize arguments and environment
    process->argc = 0;
    process->envc = 0;
    
    // Initialize linked list pointers
    process->next = NULL;
    process->prev = NULL;
    process->children = NULL;
    process->sibling = NULL;
    
    // Add to ready queue
    process->state = PROCESS_READY;
    process_add_to_list(process);
    process_count++;
    
    return process;
}

void process_destroy(process_t *process)
{
    if (!process) {
        return;
    }
    
    // Remove from all queues
    process_remove_from_list(process);
    process_remove_from_zombie_queue(process);
    
    // Free memory
    if (process->kernel_stack) {
        kfree((void *)process->kernel_stack);
    }
    
    if (process->user_stack) {
        kfree((void *)process->user_stack);
    }
    
    // Free process structure
    kfree(process);
    process_count--;
}

int process_fork(process_t *parent)
{
    if (!parent) {
        return -1;
    }
    
    // Create child process
    process_t *child = process_create(parent->name, NULL, NULL);
    if (!child) {
        return -1;
    }
    
    // Copy parent's context
    child->ppid = parent->pid;
    child->pgid = parent->pgid;
    child->priority = parent->priority;
    child->flags = parent->flags;
    
    // Copy working directory
    child->working_dir = parent->working_dir;
    
    // Copy file descriptors
    for (int i = 0; i < 16; i++) {
        child->open_files[i] = parent->open_files[i];
    }
    
    // Copy signal mask
    child->signal_mask = parent->signal_mask;
    
    // Copy arguments and environment
    child->argc = parent->argc;
    child->envc = parent->envc;
    
    // Add child to parent's children list
    child->sibling = parent->children;
    parent->children = child;
    
    return child->pid;
}

int process_exec(const char *path, char *const argv[], char *const envp[])
{
    // TODO: Implement process execution
    (void)path;
    (void)argv;
    (void)envp;
    return -1;
}

void process_exit(int exit_code)
{
    if (!current_process) {
        return;
    }
    
    // Set exit code
    current_process->exit_code = exit_code;
    current_process->state = PROCESS_ZOMBIE;
    
    // Remove from ready queue and add to zombie queue
    process_remove_from_list(current_process);
    process_add_to_zombie_queue(current_process);
    
    // TODO: Notify parent process
    // TODO: Free resources
    // TODO: Switch to another process
}

int process_wait(pid_t pid, int *status)
{
    // TODO: Implement process waiting
    (void)pid;
    (void)status;
    return -1;
}

void process_schedule(void)
{
    // TODO: Implement process scheduling
}

void process_yield(void)
{
    // TODO: Implement process yielding
}

void process_sleep(uint32_t ms)
{
    if (!current_process) {
        return;
    }
    
    // TODO: Implement process sleeping
    (void)ms;
}

void process_wakeup(process_t *process)
{
    if (!process) {
        return;
    }
    
    if (process->state == PROCESS_SLEEPING) {
        process->state = PROCESS_READY;
        process_add_to_list(process);
    }
}

void process_switch(process_t *from, process_t *to)
{
    // TODO: Implement context switching
    (void)from;
    (void)to;
}

void process_save_context(process_t *process)
{
    // TODO: Implement context saving
    (void)process;
}

void process_restore_context(process_t *process)
{
    // TODO: Implement context restoration
    (void)process;
}

process_t *process_find_by_pid(pid_t pid)
{
    // Search in ready queue
    process_t *process = ready_queue;
    while (process) {
        if (process->pid == pid) {
            return process;
        }
        process = process->next;
    }
    
    // Search in sleeping queue
    process = sleeping_queue;
    while (process) {
        if (process->pid == pid) {
            return process;
        }
        process = process->next;
    }
    
    // Search in zombie queue
    process = zombie_queue;
    while (process) {
        if (process->pid == pid) {
            return process;
        }
        process = process->next;
    }
    
    return NULL;
}

process_t *process_get_current(void)
{
    return current_process;
}

pid_t process_get_pid(void)
{
    return current_process ? current_process->pid : 0;
}

pid_t process_get_ppid(void)
{
    return current_process ? current_process->ppid : 0;
}

void process_send_signal(pid_t pid, int signal)
{
    process_t *process = process_find_by_pid(pid);
    if (!process) {
        return;
    }
    
    // TODO: Implement signal sending
    (void)signal;
}

void process_handle_signals(process_t *process)
{
    // TODO: Implement signal handling
    (void)process;
}

void process_print_info(process_t *process)
{
    if (!process) {
        return;
    }
    
    print("Process: ");
    print(process->name);
    print(" (PID: ");
    print_uint32(process->pid);
    print(")\n");
    
    print("  State: ");
    switch (process->state) {
        case PROCESS_NEW:
            print("NEW");
            break;
        case PROCESS_READY:
            print("READY");
            break;
        case PROCESS_RUNNING:
            print("RUNNING");
            break;
        case PROCESS_SLEEPING:
            print("SLEEPING");
            break;
        case PROCESS_STOPPED:
            print("STOPPED");
            break;
        case PROCESS_ZOMBIE:
            print("ZOMBIE");
            break;
        case PROCESS_DEAD:
            print("DEAD");
            break;
        default:
            print("UNKNOWN");
            break;
    }
    print("\n");
    
    print("  Priority: ");
    print_uint32(process->priority);
    print("\n");
    
    print("  CPU Time: ");
    print_uint64(process->cpu_time);
    print("\n");
}

void process_print_all(void)
{
    print("=== Process List ===\n");
    
    process_t *process = ready_queue;
    while (process) {
        process_print_info(process);
        process = process->next;
    }
    
    process = sleeping_queue;
    while (process) {
        process_print_info(process);
        process = process->next;
    }
    
    process = zombie_queue;
    while (process) {
        process_print_info(process);
        process = process->next;
    }
}

uint32_t process_get_count(void)
{
    return process_count;
}
