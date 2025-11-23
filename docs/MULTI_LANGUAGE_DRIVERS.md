# Multi-Language Driver Development for IR0 Kernel

This document provides a comprehensive guide for developing drivers and kernel components in **Rust** and **C++** for the IR0 kernel.

## Table of Contents

1. [Overview](#overview)
2. [Driver Registry System](#driver-registry-system)
3. [Rust Driver Development](#rust-driver-development)
4. [C++ Component Development](#c-component-development)
5. [Build System](#build-system)
6. [Examples](#examples)
7. [Best Practices](#best-practices)

---

## Overview

IR0 kernel supports three languages for development:

- **C**: Core kernel, memory management, legacy drivers
- **Rust**: Modern drivers (preferred for new drivers)
- **C++**: Advanced components (schedulers, network stacks)

All three languages integrate seamlessly through the **driver registry system**.

---

## Driver Registry System

### Architecture

The driver registry (`kernel/driver_registry.c`) provides a unified interface for registering and managing drivers written in any language.

### Key Features

- ✅ **Multi-language support**: C, C++, Rust
- ✅ **Lifecycle management**: Registration → Init → Active → Shutdown
- ✅ **Validation**: Input validation and error handling
- ✅ **Metadata tracking**: Name, version, author, language
- ✅ **State management**: Track driver states
- ✅ **Dynamic loading**: Runtime driver registration

### Driver Interface

```c
// Driver metadata
typedef struct ir0_driver_info {
    const char* name;           // Unique driver name
    const char* version;        // Version string
    const char* author;         // Author name
    const char* description;    // Brief description
    ir0_driver_lang_t language; // C, C++, or Rust
} ir0_driver_info_t;

// Driver operations
typedef struct ir0_driver_ops {
    // Lifecycle
    int32_t (*init)(void);
    int32_t (*probe)(void* device);
    void (*remove)(void* device);
    void (*shutdown)(void);
    
    // I/O
    int32_t (*read)(void* buf, size_t len);
    int32_t (*write)(const void* buf, size_t len);
    int32_t (*ioctl)(uint32_t cmd, void* arg);
    
    // Power management
    int32_t (*suspend)(void);
    int32_t (*resume)(void);
} ir0_driver_ops_t;

// Registration
ir0_driver_t* ir0_register_driver(const ir0_driver_info_t* info, 
                                   const ir0_driver_ops_t* ops);
```

---

## Rust Driver Development

### Prerequisites

```bash
# Install Rust
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source $HOME/.cargo/env

# Install kernel development components
rustup component add rust-src
```

### FFI Bindings

The Rust FFI module (`rust/ffi/kernel.rs`) provides safe wrappers for kernel APIs:

```rust
// Memory management
kernel_alloc(size) -> Option<*mut c_void>
kernel_free(ptr)
kernel_calloc(n, size)

// I/O ports
io_inb(port) -> u8
io_outb(port, value)
io_inw(port) -> u16
io_outw(port, value)
io_inl(port) -> u32
io_outl(port, value)

// Logging
log_info_msg(msg)
log_warn_msg(msg)
log_error_msg(msg)
log_success_msg(msg)

// Interrupts
irq_disable()
irq_enable()
irq_enabled() -> bool
```

### Writing a Rust Driver

See `rust/drivers/rust_example_driver.rs` for a complete example.

**Key points:**

1. **Use `#![no_std]`** - Freestanding environment
2. **Implement driver operations** as `extern "C"` functions
3. **Define metadata and ops structures**
4. **Provide panic handler**
5. **Use safe wrappers when possible**

### Building Rust Drivers

```bash
# Compile single driver
make unibuild -rust rust/drivers/my_driver.rs

# This will generate: rust/drivers/my_driver.o
```

### Rust Best Practices

✅ **DO:**
- Use safe wrappers (`kernel_alloc` instead of raw `kmalloc`)
- Check for null pointers
- Document unsafe blocks
- Use Result types for error handling
- Minimize unsafe code

❌ **DON'T:**
- Use standard library (std)
- Use heap allocations without cleanup
- Panic without good reason
- Ignore return values

---

## C++ Component Development

### Prerequisites

```bash
# Install G++
sudo apt-get install g++

# Verify installation
g++ --version
```

### Compatibility Layer

The C++ compatibility layer (`cpp/include/compat.h`) provides:

- **Memory operators**: Custom `new`/`delete` using `kmalloc`/`kfree`
- **ABI compatibility**: `extern "C"` macros
- **Type definitions**: Cross-language compatible types
- **Runtime support**: `__cxa_*` functions for freestanding C++

### C++ Restrictions

**Enabled:**
- ✅ Classes and templates
- ✅ Constructors/destructors (RAII)
- ✅ Operator overloading
- ✅ Namespaces
- ✅ Custom `new`/`delete`

**Disabled:**
- ❌ Exceptions (`-fno-exceptions`)
- ❌ RTTI (`-fno-rtti`)
- ❌ STL (freestanding environment)
- ❌ `<iostream>` and I/O streams
- ❌ Thread-safe statics (`-fno-threadsafe-statics`)

### Writing a C++ Component

See `cpp/examples/cpp_example.cpp` for a complete example.

**Key patterns:**

1. **RAII for resource management**:
```cpp
class ResourceGuard {
    void* resource;
public:
    ResourceGuard(size_t size) {
        resource = kmalloc(size);
    }
    ~ResourceGuard() {
        if (resource) kfree(resource);
    }
};
```

2. **Templates for type safety**:
```cpp
template<typename T, size_t Size>
class CircularBuffer {
    T buffer[Size];
    // ...
};
```

3. **C interface for kernel integration**:
```cpp
namespace ir0 {
    class MyComponent { /* C++ implementation */ };
}

extern "C" {
    IR0_API int32_t my_component_init() {
        // Bridge to C++ code
    }
}
```

### Building C++ Components

```bash
# Compile single component
make unibuild -cpp cpp/my_component.cpp

# This will generate: cpp/my_component.o
```

### C++ Best Practices

✅ **DO:**
- Use RAII for automatic resource management
- Use templates for generic code
- Provide C interface for kernel integration
- Use namespaces to organize code
- Prefer stack allocation when possible

❌ **DON'T:**
- Use exceptions
- Use RTTI or `dynamic_cast`
- Use STL containers
- Rely on global constructors excessively
- Use thread_local storage

---

## Build System

### Unified Build Command

The `unibuild` system supports all languages:

```bash
# C compilation (existing)
make unibuild kernel/module.c

# C++ compilation (new)
make unibuild -cpp kernel/scheduler.cpp

# Rust compilation (new)
make unibuild -rust rust/drivers/network.rs

# Cross-compilation to Windows
make unibuild -win -cpp kernel/module.cpp
make unibuild -win kernel/module.c
```

### Compiler Flags

**Rust:**
- `--edition 2021`
- `--target x86_64-ir0-kernel.json` (custom target)
- `-C opt-level=2`
- `-C panic=abort`
- `-C code-model=kernel`

**C++:**
- `-fno-exceptions -fno-rtti -fno-threadsafe-statics`
- `-ffreestanding -nostdlib`
- `-mcmodel=large -mno-red-zone`
- `-mno-mmx -mno-sse -mno-sse2`

### Integration with Makefile

Both `cpp/runtime/compat.o` and `kernel/driver_registry.o` are automatically included in the kernel build.

---

## Examples

### Rust Driver Example

```bash
# View the example
cat rust/drivers/rust_example_driver.rs

# Compile it
make unibuild -rust rust/drivers/rust_example_driver.rs

# The driver demonstrates:
# - Memory allocation and I/O ports
# - Logging functions
# - Driver lifecycle
# - State management
```

### C++ Component Example

```bash
# View the example
cat cpp/examples/cpp_example.cpp

# Compile it
make unibuild -cpp cpp/examples/cpp_example.cpp

# The component demonstrates:
# - RAII resource management
# - Template-based circular buffer
# - OOP design patterns
# - C interface for kernel integration
```

---

## Best Practices

### General Guidelines

1. **Always validate inputs** - Check for NULL pointers, invalid sizes
2. **Clean up resources** - Free allocated memory, close file descriptors
3. **Use appropriate logging** - `log_info`, `log_error`, etc.
4. **Follow coding conventions** - Consistent style across languages
5. **Document your code** - Especially `unsafe` blocks and complex logic
6. **Test thoroughly** - Use `make run-debug` to test drivers

### Error Handling

**Rust:**
```rust
match kernel_alloc(size) {
    Some(ptr) => { /* use ptr */ },
    None => {
        log_error_msg("Allocation failed\n");
        return IR0_DRIVER_ERR_NOMEM;
    }
}
```

**C++:**
```cpp
void* ptr = kmalloc(size);
if (!ptr) {
    log_error("[C++] Allocation failed");
    return IR0_DRIVER_ERR_NOMEM;
}
// Use RAII to ensure cleanup
```

### Memory Management

- **Always free what you allocate**
- **Use RAII in C++ for automatic cleanup**
- **Check allocation results before use**
- **Avoid memory leaks - valgrind-style thinking**

### Performance

- **Minimize allocations in hot paths**
- **Use stack when possible**
- **Prefer compile-time over runtime where feasible**
- **Profile and measure - don't guess**

---

## Troubleshooting

### Rust Compilation Issues

**Problem**: `rustc: command not found`
```bash
# Install Rust
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

**Problem**: "no target specification" error
```bash
# Ensure x86_64-ir0-kernel.json exists
ls rust/x86_64-ir0-kernel.json
```

### C++ Compilation Issues

**Problem**: `g++: command not found`
```bash
sudo apt-get install g++
```

**Problem**: Linker errors with `new`/`delete`
```bash
# Ensure cpp/runtime/compat.o is linked
# It should be in CPP_OBJS in Makefile
```

### Driver Registration Issues

**Problem**: Driver not initializing
```bash
# Check kernel boot logs
make run-debug

# Verify driver registration was called
# Look for "Registered driver: <name>" in logs
```

---

## Additional Resources

- **Rust FFI**: `rust/ffi/kernel.rs`
- **C++ Compatibility**: `cpp/include/compat.h`
- **Driver Interface**: `includes/ir0/driver.h`
- **Main Documentation**: `CONTRIBUTING.md`

---

## License

All driver code must be licensed under GPL-3.0-only to be compatible with the IR0 kernel.

```
// SPDX-License-Identifier: GPL-3.0-only
```
