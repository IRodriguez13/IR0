// syscalls.c - System call wrappers for IR0 libc
#include <stdint.h>
#include <sys/types.h>
#include <ir0/syscall.h> 

// System call implementations using ir0/syscall.h

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

int brk(void *addr)
{
    return ir0_brk(addr);
}

void mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    ir0_mmap(addr, length, prot, flags, fd, offset);
}