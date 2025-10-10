#pragma once

#include <stddef.h>
#include <stdint.h>

// File descriptors
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// Basic I/O functions
int printf(const char *format, ...);
int puts(const char *str);
int putchar(int c);

// File operations
int open(const char *pathname, int flags);
int close(int fd);
int read(int fd, void *buf, size_t count);
int write(int fd, const void *buf, size_t count);

