// syscalls.c - System call wrappers for IR0 libc
#include <stdint.h>
#include <sys/types.h>
#include <ir0/syscall.h> // Use kernel's syscall interface

// System call implementations using ir0/syscall.h
void exit(int status)
{
    ir0_exit(status);
}

int write(int fd, const void *buf, size_t count)
{
    return (int)ir0_write(fd, buf, count);
}

int read(int fd, void *buf, size_t count)
{
    return (int)ir0_read(fd, buf, count);
}

pid_t getpid(void)
{
    return ir0_getpid();
}

pid_t getppid(void)
{
    return syscall0(SYS_GETPPID); // Not implemented in ir0/syscall.h yet
}

pid_t fork(void)
{
    return ir0_fork();
}

pid_t waitpid(pid_t pid, int *status, int options)
{
    (void)options; // Ignore options for now
    return ir0_waitpid(pid, status);
}

void *sbrk(intptr_t increment)
{
    return ir0_sbrk(increment);
}

int brk(void *addr)
{
    return ir0_brk(addr);
}