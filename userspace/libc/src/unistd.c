// unistd.c - Basic POSIX-like wrappers for IR0 libc
#include <unistd.h>
#include <ir0/syscall.h>

int open(const char *pathname, int flags) {
    // Mode is not used in this simple implementation; default to 0
    return (int)ir0_open(pathname, flags, 0);
}

int close(int fd) {
    return (int)ir0_close(fd);
}

// The read and write functions are already provided in syscalls.c, but we
// provide thin wrappers here for completeness.
int read(int fd, void *buf, size_t count) {
    return (int)ir0_read(fd, buf, count);
}

int write(int fd, const void *buf, size_t count) {
    return (int)ir0_write(fd, buf, count);
}

void exit(int status) {
    ir0_exit(status);
    __builtin_unreachable();
}

void *sbrk(intptr_t increment) {
    return ir0_sbrk(increment);
}
