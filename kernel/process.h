#pragma once

#include <stdint.h>
#include <stddef.h>
#include <kernel/scheduler/task.h>

// Process states
typedef enum {
    PROCESS_READY = 0,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_ZOMBIE,
    PROCESS_TERMINATED
} process_state_t;

// Process ID type
typedef uint32_t pid_t;

// Extended process structure for fork/exec
typedef struct process {
    // Task compatibility (debe ser el primer campo)
    task_t task;
    
    // Process hierarchy
    pid_t ppid;                    // Parent PID
    struct process *parent;        // Parent process
    struct process *children;      // First child
    struct process *sibling;       // Next sibling
    
    // Memory management
    uint64_t *page_directory;      // Page directory (CR3)
    uint64_t heap_start;           // Heap start address
    uint64_t heap_end;             // Current heap end
    uint64_t stack_start;          // Stack start
    uint64_t stack_size;           // Stack size
    
    // File descriptors
    struct file_descriptor *fd_table[16];  // File descriptor table
    
    // Process state
    process_state_t state;
    int exit_code;
    
    // Next process in global list
    struct process *next;
} process_t;

// Macros para acceder a los registros a través de task_t
#define process_rax(p) ((p)->task.rax)
#define process_rbx(p) ((p)->task.rbx)
#define process_rcx(p) ((p)->task.rcx)
#define process_rdx(p) ((p)->task.rdx)
#define process_rsi(p) ((p)->task.rsi)
#define process_rdi(p) ((p)->task.rdi)
#define process_rsp(p) ((p)->task.rsp)
#define process_rbp(p) ((p)->task.rbp)
#define process_rip(p) ((p)->task.rip)
#define process_rflags(p) ((p)->task.rflags)
#define process_cs(p) ((p)->task.cs)
#define process_ss(p) ((p)->task.ss)
#define process_ds(p) ((p)->task.ds)
#define process_es(p) ((p)->task.es)
#define process_fs(p) ((p)->task.fs)
#define process_gs(p) ((p)->task.gs)
#define process_pid(p) ((p)->task.pid)

// Global current process
extern process_t *current_process;
extern process_t *idle_process;

// Process management functions
void process_init(void);
process_t *process_create(void (*entry)(void *), void *arg, uint8_t priority, int8_t nice);
void process_destroy(process_t *process);
void process_exit(int exit_code);
int process_wait(pid_t pid, int *status);

// NEW: fork/exec functions
pid_t process_fork(void);
int process_exec(const char *filename, char *const argv[], char *const envp[]);
process_t *process_duplicate(process_t *parent);
int process_copy_memory(process_t *parent, process_t *child);
void process_setup_child(process_t *child, process_t *parent);

// Context switching
void process_wakeup(process_t *process);
void process_switch(process_t *from, process_t *to);
void process_save_context(process_t *process);
void process_restore_context(process_t *process);

// Process lookup
process_t *process_find_by_pid(pid_t pid);
process_t *process_get_current(void);
pid_t process_get_pid(void);
pid_t process_get_ppid(void);

// Signals (for later)
void process_send_signal(pid_t pid, int signal);
void process_handle_signals(process_t *process);

// Debug/info
void process_print_info(process_t *process);
void process_print_all(void);
process_t *get_process_list(void);

// File descriptor structure (simple)
struct file_descriptor {
    int flags;
    uint64_t offset;
    void *private_data;
};

