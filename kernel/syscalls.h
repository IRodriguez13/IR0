#pragma once

#include <stdint.h>
#include <stddef.h>
#include <ir0/stat.h>

// Forward declarations
typedef uint32_t mode_t;

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
int64_t sys_mkdir(const char *pathname, mode_t mode);
int64_t sys_rmdir(const char *pathname);
int64_t sys_fstat(int fd, stat_t *buf);
int64_t sys_stat(const char *pathname, stat_t *buf);
int64_t sys_open(const char *pathname, int flags, mode_t mode);
int64_t sys_close(int fd);

// Syscall handler
int64_t syscall_handler(uint64_t number, syscall_args_t *args);

// Syscall initialization
void syscalls_init(void);

// Test function
void test_user_function(void);

