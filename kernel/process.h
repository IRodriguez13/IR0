#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <kernel/scheduler/task.h>

typedef uint32_t pid_t;
typedef long off_t;

#define MAX_FDS_PER_PROCESS 32

typedef struct fd_entry {
    bool in_use;
    char path[256];
    int flags;
    off_t offset;
    void *vfs_file;
} fd_entry_t;

typedef enum {
    PROCESS_READY = 0,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_ZOMBIE
} process_state_t;

typedef struct process {
    task_t task;
    pid_t ppid;
    struct process *parent;
    struct process *children;
    struct process *sibling;
    uint64_t *page_directory;
    uint64_t heap_start;
    uint64_t heap_end;
    uint64_t stack_start;
    uint64_t stack_size;
    process_state_t state;
    int exit_code;
    struct process *next;
    fd_entry_t fd_table[MAX_FDS_PER_PROCESS];
} process_t;

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

extern process_t *current_process;
extern process_t *process_list;

void process_init(void);
pid_t process_fork(void);
void process_exit(int code);
int process_wait(pid_t pid, int *status);

pid_t process_get_pid(void);
pid_t process_get_ppid(void);
process_t *process_get_current(void);
process_t *get_process_list(void);
pid_t process_get_next_pid(void);

uint64_t create_process_page_directory(void);
void process_init_fd_table(process_t *process);
