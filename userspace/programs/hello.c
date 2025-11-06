// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: hello.c
 * Description: Simple Hello World program for testing userspace execution
 */

// Simple syscall interface
static inline long syscall1(long number, long arg1) {
    long result;
    __asm__ volatile (
        "int $0x80"
        : "=a" (result)
        : "a" (number), "D" (arg1)
        : "memory"
    );
    return result;
}

static inline long syscall3(long number, long arg1, long arg2, long arg3) {
    long result;
    __asm__ volatile (
        "int $0x80"
        : "=a" (result)
        : "a" (number), "D" (arg1), "S" (arg2), "d" (arg3)
        : "memory"
    );
    return result;
}

// Syscall numbers
#define SYS_EXIT 0
#define SYS_WRITE 1

// File descriptors
#define STDOUT_FILENO 1

// Simple write function
static void write_string(const char *str) {
    // Calculate string length
    int len = 0;
    while (str[len]) len++;
    
    // Write to stdout
    syscall3(SYS_WRITE, STDOUT_FILENO, (long)str, len);
}

// Program entry point
void _start(void) {
    write_string("Hello, World from userspace!\n");
    write_string("IR0 Kernel is working!\n");
    
    // Exit with success code
    syscall1(SYS_EXIT, 0);
    
    // Should never reach here
    for (;;);
}