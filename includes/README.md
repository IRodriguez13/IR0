# IR0 Includes and Standard Library

This directory contains header files and freestanding C library implementation for IR0.

## Components

### Standard Headers
- `stdint.h` - Standard integer types (uint8_t, uint32_t, etc.)
- `stddef.h` - Standard definitions (size_t, NULL, etc.)
- `stdarg.h` - Variable argument support for printf
- `string.h/c` - String manipulation functions

### IR0-Specific Headers (`ir0/`)
- `print.h/c` - Kernel printing and output functions
- `logging.h/c` - Kernel logging system with multiple levels
- `validation.h/c` - Input validation and error checking
- `syscall.h` - System call interface and wrappers
- `panic/panic.c/h` - Kernel panic handling

## Features

### Freestanding C Library
- ✅ Complete standard type definitions
- ✅ Basic string manipulation functions
- ✅ Variable argument support (va_list, va_start, va_end)
- ✅ Memory utility functions
- ✅ Character classification functions

### String Functions
```c
// String manipulation
size_t strlen(const char *str);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
int strcmp(const char *str1, const char *str2);
int strncmp(const char *str1, const char *str2, size_t n);
char *strcat(char *dest, const char *src);
char *strchr(const char *str, int c);
char *strstr(const char *haystack, const char *needle);

// Memory functions
void *memset(void *ptr, int value, size_t num);
void *memcpy(void *dest, const void *src, size_t num);
void *memmove(void *dest, const void *src, size_t num);
int memcmp(const void *ptr1, const void *ptr2, size_t num);
```

### Kernel Print System
- ✅ Basic text output to VGA buffer
- ✅ Hexadecimal number printing
- ✅ Decimal number printing
- ✅ String printing
- ✅ Character printing
- ✅ Cursor management
- ✅ Screen clearing

### Logging System
```c
// Logging levels
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3,
    LOG_LEVEL_CRITICAL = 4
} log_level_t;

// Logging functions
void log_debug(const char *message);
void log_info(const char *message);
void log_warn(const char *message);
void log_error(const char *message);
void log_critical(const char *message);
void log_subsystem_ok(const char *subsystem);
```

### System Call Interface
- ✅ 23 system call wrappers
- ✅ Low-level syscall functions (syscall0-6)
- ✅ High-level POSIX-like wrappers
- ✅ Process management calls
- ✅ File system calls
- ✅ Memory management calls

### Validation System
- ✅ Pointer validation
- ✅ Range checking
- ✅ Input sanitization
- ✅ Error code definitions
- ✅ Bounds checking

### Panic System
- ✅ Kernel panic handling
- ✅ Stack trace printing
- ✅ Error information display
- ✅ System halt on critical errors
- ✅ Debug information preservation

## Architecture

### Freestanding Environment
The library is designed for a freestanding C environment:
- No standard library dependencies
- Custom implementations of all functions
- Kernel-specific optimizations
- Memory-safe implementations

### Integration with Kernel
- Direct VGA buffer access for printing
- Serial output for debugging
- Integration with memory management
- Syscall interface for userspace

## Current Status

### ✅ Fully Implemented
- Standard type definitions
- String manipulation functions
- Memory utility functions
- Kernel printing system
- Logging system with multiple levels
- System call interface
- Validation utilities
- Panic handling

### ✅ Working Features
- Text output to screen
- String operations
- Memory operations
- Kernel logging
- System call wrappers
- Error handling

### ⚠️ Limitations
- Basic implementations (not optimized)
- Limited floating-point support
- No locale support
- Basic error handling

### 🔄 Potential Improvements
- Optimized string functions
- Better memory functions
- Enhanced logging features
- More validation utilities
- Extended panic information

## Build Integration

The includes are built as part of the kernel:
- Compiled with freestanding flags
- No external dependencies
- Architecture-specific optimizations
- Debug symbol generation

## Usage

### In Kernel Code
```c
#include <stdint.h>
#include <string.h>
#include <ir0/print.h>
#include <ir0/logging.h>

void example_function(void) {
    print("Hello from kernel!\n");
    log_info("System initialized");
    
    char buffer[64];
    strcpy(buffer, "Test string");
    print(buffer);
}
```

### In Userspace Code
```c
#include <stdint.h>
#include <ir0/syscall.h>

int main(void) {
    ir0_write(1, "Hello from userspace!\n", 22);
    return 0;
}
```