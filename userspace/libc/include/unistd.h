#pragma once

#include <stddef.h>
#include <sys/types.h>
#include <ir0/types.h> 

// Process management
pid_t fork(void);
int execv(const char *path, char *const argv[]);
int execve(const char *path, char *const argv[], char *const envp[]);
pid_t getpid(void);
pid_t getppid(void);
pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int options);

// File operations
int read(int fd, void *buf, size_t count);
int write(int fd, const void *buf, size_t count);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);

// Directory operations
int chdir(const char *path);
char *getcwd(char *buf, size_t size);

// Memory management
void *sbrk(intptr_t increment);
int brk(void *addr);

