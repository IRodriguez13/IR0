#ifndef IR0_STDLIB_H
#define IR0_STDLIB_H

#include <stddef.h>
#include <stdint.h>

// IR0 kernel-provided utility functions (not standard C)
// Use: #include <ir0/stdlib.h>

// Memory management (kernel helpers, not userland malloc)
void *ir0_kmalloc(size_t size);
void ir0_kfree(void *ptr);

// String conversion (kernel helpers)
int ir0_atoi(const char *str);
long ir0_atol(const char *str);

// Process control (kernel helpers)
void ir0_panic(const char *msg);
void ir0_reboot(void);

// Add more kernel-specific helpers as needed

#endif // IR0_STDLIB_H
