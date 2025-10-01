#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <stdint.h>
#include <stddef.h>

// Syscall arguments structure
typedef struct {
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint64_t arg4;
    uint64_t arg5;
    uint64_t arg6;
} syscall_args_t;

// Syscall implementations
int64_t sys_exit(int exit_code);
int64_t sys_write(int fd, const void *buf, size_t count);
int64_t sys_read(int fd, void *buf, size_t count);
int64_t sys_getpid(void);
int64_t sys_getppid(void);
int64_t sys_ls(const char *pathname);

// Syscall handler
int64_t syscall_handler(uint64_t number, syscall_args_t *args);

// Syscall initialization
void syscalls_init(void);

// Test function
void test_user_function(void);

#endif // SYSCALLS_H
