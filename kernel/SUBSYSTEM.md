# Kernel Subsystem

Core kernel functionality including process management, scheduling, and system calls.

## Architecture

```
kernel/
├── process.c/h          - Process lifecycle (fork, exit, wait)
├── rr_sched.c/h         - Round-robin scheduler
├── syscalls.c/h         - System call dispatcher
├── syscalls_internal.h  - Internal syscall implementations
├── errno_defs.h         - POSIX error codes
├── init.c/h             - PID 1 init process
├── shell.c/h            - Built-in shell
├── elf_loader.c/h       - ELF binary loader
└── main.c               - Kernel entry point
```

## Public Interfaces

### Process Management (`process.h`)

**Lifecycle:**
- `void process_init(void)` - Initialize process subsystem
- `pid_t process_fork(void)` - Create child process
- `void process_exit(int code)` - Terminate current process
- `int process_wait(pid_t pid, int *status)` - Wait for child

**Queries:**
- `pid_t process_get_pid(void)` - Get current PID
- `pid_t process_get_ppid(void)` - Get parent PID
- `process_t *process_get_current(void)` - Get current process
- `process_t *get_process_list(void)` - Get all processes

**Memory:**
- `uint64_t create_process_page_directory(void)` - Create process page tables
- `void process_init_fd_table(process_t *)` - Initialize file descriptors

### Scheduler (`rr_sched.h`)

- `void rr_add_process(process_t *proc)` - Add process to scheduler
- `void rr_schedule_next(void)` - Switch to next process
- `void rr_remove_process(process_t *proc)` - Remove from scheduler

### System Calls (`syscalls.h`)

- `void syscalls_init(void)` - Initialize syscall subsystem
- `int64_t syscall_handler(...)` - Main syscall dispatcher

See `syscalls_internal.h` for individual syscall implementations.

## Error Codes

All error codes are defined in `<errno.h>` following POSIX standards.
Use negative error codes for syscall returns: `return -EINVAL;`

Error strings available via `strerror(int errnum)` in `errno.c`.

## Process States

```c
typedef enum {
    PROCESS_READY = 0,    /* Ready to run */
    PROCESS_RUNNING,      /* Currently executing */
    PROCESS_BLOCKED,      /* Waiting for I/O */
    PROCESS_ZOMBIE        /* Terminated, waiting for parent */
} process_state_t;
```

## Memory Layout

Each process has:
- **Code segment**: Loaded from ELF binary
- **Stack**: 8KB default, grows downward
- **Heap**: Managed by `brk()` syscall
- **Page directory**: Isolated virtual address space

## Scheduling

Current implementation: **Round-robin** with 10ms time slices.

Future: CFS (Completely Fair Scheduler) in `kernel/scheduler/`

## System Call Convention

**x86-64:**
- Syscall number in `RAX`
- Arguments in `RDI, RSI, RDX, R10, R8, R9`
- Return value in `RAX`
- Invoked via `int 0x80` or `syscall` instruction

## Dependencies

- `ir0/memory/kmem.h` - Kernel memory allocator
- `ir0/memory/paging.h` - Page table management
- `drivers/serial/serial.h` - Debug output
- `arch/*/user_mode.h` - Ring 3 transition

## Thread Safety

**NOT thread-safe**. Assumes single-core, cooperative multitasking.
Interrupts are disabled during critical sections.

## Future Work

- [ ] Preemptive multitasking
- [ ] SMP support
- [ ] Process priorities
- [ ] Real-time scheduling
- [ ] Thread support (POSIX threads)
