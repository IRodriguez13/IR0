#ifndef PROCESS_H
#define PROCESS_H

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

// Process structure (compatible with task_t)
typedef task_t process_t;

// Global current process
extern process_t *current_process;
extern process_t *idle_process;

// Process management functions
void process_init(void);
process_t *process_create(void (*entry)(void *), void *arg, uint8_t priority, int8_t nice);
void process_destroy(process_t *process);
void process_exit(int exit_code);
int process_wait(pid_t pid, int *status);
void process_wakeup(process_t *process);
void process_switch(process_t *from, process_t *to);
void process_save_context(process_t *process);
void process_restore_context(process_t *process);
process_t *process_find_by_pid(pid_t pid);
process_t *process_get_current(void);
pid_t process_get_pid(void);
pid_t process_get_ppid(void);
void process_send_signal(pid_t pid, int signal);
void process_handle_signals(process_t *process);
void process_print_info(process_t *process);
void process_print_all(void);
process_t *get_process_list(void);

#endif // PROCESS_H
