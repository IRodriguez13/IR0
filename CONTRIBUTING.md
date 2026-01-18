# IR0 Kernel - Contributing Guide

Este documento proporciona información completa para desarrolladores trabajando en el kernel IR0, incluyendo detalles de arquitectura, estándares de codificación, y flujos de trabajo de desarrollo.

---

## 📋 Table of Contents

1. [Required Dependencies](#required-dependencies)
2. [Architecture Overview](#architecture-overview)
3. [Multi-Language Development](#multi-language-development)
   - [Rust Driver Development](#rust-driver-development)
   - [C++ Kernel Components](#c-kernel-components)
4. [Core Subsystems](#core-subsystems)
5. [Development Workflow](#development-workflow)
6. [Code Style Guidelines](#code-style-guidelines)
7. [Testing Guidelines](#testing-guidelines)

---

## Required Dependencies

### Essential Build Tools
- **GCC** (GNU Compiler Collection 7.0+)
- **Make** (build automation)
- **NASM** (Netwide Assembler 2.13+)
- **LD** (GNU Linker)
- **AR** (GNU Archiver)

### Multi-Language Support (Optional)
- **G++** (C++ compiler for kernel components)
- **Rust** (rustc + cargo for drivers)
  - Install: `curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh`
- **MinGW-w64** (for cross-compilation to Windows)

### Bootable Image Tools
- **GRUB** (grub-pc-bin)
- **Xorriso** (ISO creation)

### Emulation and Testing
- **QEMU** (qemu-system-x86)

### Recommended
- **Git** (version control)
- **Python 3** with tkinter (for menuconfig)
- **GDB** (debugging)

### Dependency Verification

Run the dependency checker:
```bash
make deptest
```

This will verify:
- ✓ C compiler (GCC/Clang)
- ✓ C++ compiler (G++/Clang++)
- ✓ Rust compiler (rustc + cargo)
- ✓ MinGW cross-compilers (Linux only)
- ✓ Python 3 with tkinter and PIL
- ✓ NASM, QEMU, GRUB

---

## Architecture Overview

### Kernel Design Philosophy

The IR0 kernel follows these principles:

1. **Modularity**: Subsystems are independent and configurable
2. **Portability**: Architecture-agnostic design
3. **Multi-Language**: C core with Rust drivers and C++ components
4. **Educational**: Clear structure for learning
5. **Performance**: Efficient algorithms
6. **Extensibility**: Easy to add features

---

## Multi-Language Development

IR0 supports **C, C++, and Rust** for different purposes:

- **C**: Kernel core, memory management, core drivers
- **Rust**: New device drivers (network, storage, USB, etc.)
- **C++**: Advanced kernel components (schedulers, network stacks)

### Language Usage Guidelines

| Component | C | Rust | C++ |
|-----------|---|------|-----|
| Kernel Core | ✅ Primary | ❌ No | ⚠️ Very Limited |
| Memory Management | ✅ Yes | ❌ No | ❌ No |
| Device Drivers | ✅ Legacy | ✅ Preferred | ✅ Preferred |
| Schedulers | ✅ Yes | ❌ No | ✅ Advanced only |
| Network Stack | ✅ Yes | ❌ No | ✅ Optional |
| Userspace | ✅ Utilities | ✅ Apps/Libraries | ✅ Apps/Libraries |

---

## Rust Driver Development

### Introduction

Rust provides **memory safety** without runtime overhead, making it ideal for device drivers. All new drivers should be written in Rust when possible.

### Why Rust for Drivers?

✅ **Memory Safety**: No buffer overflows, use-after-free, or null pointer dereferences  
✅ **Type Safety**: Compiler catches errors at compile time  
✅ **Zero-Cost Abstractions**: No runtime penalty  
✅ **Clear Ownership**: Prevents data races  
✅ **FFI Integration**: Easy C interoperability

### Project Structure

```
ir0-kernel/
├── rust/
│   ├── ffi/
│   │   ├── kernel.rs      # Kernel API bindings
│   │   └── README.md
│   └── drivers/           # Rust drivers
│       ├── network/
│       ├── storage/
│       └── usb/
├── drivers/              # C drivers (legacy)
└── Makefile
```

### Setting Up Rust Environment

1. **Install Rust**:
```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source $HOME/.cargo/env
```

2. **Install required components**:
```bash
rustup component add rust-src
rustup target add x86_64-unknown-none
```

3. **Verify installation**:
```bash
rustc --version
cargo --version
```

### Kernel FFI Bindings

The kernel provides Rust bindings in `rust/ffi/kernel.rs`:

```rust
use ir0::ffi::kernel::*;

// Memory allocation
let ptr = kernel_alloc(1024)?;
kernel_free(ptr);

// Printing
kernel_print("Hello from Rust!\n");

// Panic handling
if error {
    kernel_panic("Fatal error");
}
```

### Writing a Rust Driver

#### Step 1: Create Driver File

Create `rust/drivers/network/my_driver.rs`:

```rust
// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Rust Driver
 * Copyright (C) 2025  Your Name
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 */

#![no_std]
#![no_main]

use core::ffi::c_void;

// Import kernel FFI
extern crate ir0_ffi;
use ir0_ffi::*;

/// Driver state structure
struct MyDriver {
    initialized: bool,
    device_addr: usize,
}

impl MyDriver {
    const fn new() -> Self {
        Self {
            initialized: false,
            device_addr: 0,
        }
    }
}

static mut DRIVER: MyDriver = MyDriver::new();

/// Initialize driver
#[no_mangle]
pub extern "C" fn my_driver_init() -> i32 {
    kernel_print("My Rust driver initializing...\n");
    
    unsafe {
        DRIVER.initialized = true;
    }
    
    kernel_print("My Rust driver initialized successfully\n");
    0  // IR0_OK
}

/// Probe device
#[no_mangle]
pub extern "C" fn my_driver_probe(device: *mut c_void) -> i32 {
    if device.is_null() {
        kernel_panic("NULL device pointer");
    }
    
    kernel_print("Probing device...\n");
    
    // Probe logic here
    
    0  // IR0_OK
}

/// Remove device
#[no_mangle]
pub extern "C" fn my_driver_remove(device: *mut c_void) {
    kernel_print("Removing device...\n");
    
    // Cleanup logic here
}

/// Read from device
#[no_mangle]
pub extern "C" fn my_driver_read(buf: *mut c_void, len: usize) -> i32 {
    if buf.is_null() {
        return -1;  // ERROR
    }
    
    // Read logic here
    
    len as i32
}

/// Write to device
#[no_mangle]
pub extern "C" fn my_driver_write(buf: *const c_void, len: usize) -> i32 {
    if buf.is_null() {
        return -1;  // ERROR
    }
    
    // Write logic here
    
    len as i32
}

/// Driver operations structure
#[no_mangle]
static DRIVER_OPS: DriverOps = DriverOps {
    init: Some(my_driver_init),
    probe: Some(my_driver_probe),
    remove: Some(my_driver_remove),
    read: Some(my_driver_read),
    write: Some(my_driver_write),
    ioctl: None,
};

/// Register driver with kernel
### Rust Driver Development

Rust drivers are integrated into the IR0 kernel as static libraries linked at build time. The build system uses `cargo` with the `build-std` feature to compile the Rust core library for the custom bare-metal target.

#### 1. Environment Setup

To develop Rust drivers, you must configure the Rust toolchain for bare-metal development. The `nightly` channel is required to access the unstable `build-std` feature.

```bash
# Install Rust (if not already installed)
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

# Install Nightly Toolchain
rustup toolchain install nightly

# Install Rust Source Code (Required for recompiling libcore)
rustup component add rust-src --toolchain nightly

# Add the bare-metal target (optional, handled automatically by unibuild)
rustup target add x86_64-unknown-none
```

#### 2. Driver Structure

A Rust driver consists of a single `.rs` file (or a module structure) that must adhere to the following requirements:

*   **No Standard Library**: Must use `#![no_std]` as the kernel environment does not support the full Rust standard library.
*   **No Main Function**: Must use `#![no_main]` as the entry point is controlled by the kernel.
*   **FFI Bindings**: Must declare external kernel functions using `extern "C"`.
*   **Thread Safety**: Static structures containing raw pointers must implement `unsafe impl Sync`.

**Example Driver Template:**

```rust
#![no_std]
#![no_main]

use core::ffi::c_void;

// Kernel FFI Bindings (External Functions)
extern "C" {
    fn kernel_print(msg: *const u8);
    fn kmalloc(size: usize) -> *mut c_void;
    fn kfree(ptr: *mut c_void);
    fn ir0_register_driver(info: *const DriverInfo, ops: *const DriverOps) -> *mut c_void;
}

// Driver Information Structure
#[repr(C)]
struct DriverInfo {
    name: *const u8,
    version: *const u8,
    author: *const u8,
    description: *const u8,
    language: i32, // 2 = Rust
}

// Driver Operations Structure
#[repr(C)]
struct DriverOps {
    init: Option<extern "C" fn() -> i32>,
    probe: Option<extern "C" fn() -> i32>,
    remove: Option<extern "C" fn() -> i32>,
    shutdown: Option<extern "C" fn() -> i32>,
    // ... other operations
}

// Driver Initialization Function
extern "C" fn my_driver_init() -> i32 {
    unsafe { kernel_print(b"Rust driver initialized\n\0".as_ptr()) };
    0 // Success
}

// Static Driver Definitions
static DRIVER_INFO: DriverInfo = DriverInfo {
    name: b"my_rust_driver\0".as_ptr(),
    version: b"1.0.0\0".as_ptr(),
    author: b"Author Name\0".as_ptr(),
    description: b"Description\0".as_ptr(),
    language: 2,
};
unsafe impl Sync for DriverInfo {}

static DRIVER_OPS: DriverOps = DriverOps {
    init: Some(my_driver_init),
    probe: None,
    remove: None,
    shutdown: None,
};
unsafe impl Sync for DriverOps {}

// Registration Entry Point
#[no_mangle]
pub extern "C" fn register_my_driver() -> *mut c_void {
    unsafe { ir0_register_driver(&DRIVER_INFO, &DRIVER_OPS) }
}

// Panic Handler (Required for no_std)
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}
```

#### 3. Compilation and Management

The `Makefile` provides commands to manage the driver lifecycle:

*   **Compile Single File**: `make unibuild-rust rust/drivers/my_driver.rs`
*   **Load All Rust Drivers**: `make load-driver rust`
*   **Clean Rust Objects**: `make unload-driver rust`

### C++ Driver Development

C++ drivers allow for object-oriented design within the kernel. The environment provides a minimal runtime to support essential C++ features.

#### 1. Runtime Support

The kernel includes a compatibility layer (`compat.cpp`) that provides:
*   `new` and `delete` operators (using kernel heap).
*   Pure virtual function handlers.
*   Static initialization guards.
*   **Note**: Exceptions (`try`/`catch`) and RTTI (`dynamic_cast`, `typeid`) are **disabled** to reduce overhead.

#### 2. Driver Structure

C++ drivers should follow the standard kernel driver interface defined in `includes/ir0/driver.h`.

**Example Driver Template:**

```cpp
#include <ir0/driver.h>
#include <ir0/logging.h>

// Driver Class
class MyDriver {
public:
    MyDriver() {
        LOG_INFO("MyDriver", "Constructor called");
    }
    
    ~MyDriver() {
        LOG_INFO("MyDriver", "Destructor called");
    }
    
    int init() {
        return 0;
    }
};

// Global Instance
static MyDriver* g_driver = nullptr;

// C-Compatible Entry Points
extern "C" {

    int my_driver_init() {
        g_driver = new MyDriver();
        return g_driver->init();
    }

    int my_driver_shutdown() {
        if (g_driver) {
            delete g_driver;
            g_driver = nullptr;
        }
        return 0;
    }

    // Driver Registration
    void register_cpp_driver() {
        static ir0_driver_info_t info = {
            .name = "cpp_driver",
            .version = "1.0",
            .author = "Author",
            .description = "C++ Driver",
            .language = IR0_DRIVER_LANG_CPP
        };
        
        static ir0_driver_ops_t ops = {
            .init = my_driver_init,
            .shutdown = my_driver_shutdown
        };
        
        ir0_register_driver(&info, &ops);
    }
}
```

#### 3. Compilation

*   **Compile Single File**: `make unibuild-cpp cpp/drivers/my_driver.cpp`
*   **Load All C++ Drivers**: `make load-driver cpp`
*   **Clean C++ Objects**: `make unload-driver cpp`

### Rust Best Practices for Kernel Development

#### Memory Management

```rust
// Allocate memory
let ptr = match kernel_alloc(1024) {
    Some(p) => p,
    None => {
        kernel_panic("Out of memory");
    }
};

// Always free memory
kernel_free(ptr);
```

#### Error Handling

```rust
// Use Result for fallible operations
fn do_operation() -> Result<(), i32> {
    if condition {
        Ok(())
    } else {
        Err(-1)
    }
}

// Handle errors properly
match do_operation() {
    Ok(_) => kernel_print("Success\n"),
    Err(e) => kernel_print("Error occurred\n"),
}
```

#### Safety

```rust
// Minimize unsafe code
unsafe {
    // Only use unsafe when absolutely necessary
    // Document why it's safe
}

// Prefer safe wrappers
let result = safe_wrapper(data);  // Better
let result = unsafe { raw_call(data) };  // Avoid
```

### Debugging Rust Drivers

```rust
// Use kernel_print for debugging
kernel_print(&format!("Value: {}\n", value));

// Use macros
check_ptr!(ptr, "my_function");
kernel_assert!(condition, "assertion failed");
```

### Rust Driver Examples

See `rust/drivers/` for complete examples:
- `network/rtl8139.rs` - Network card driver
- `storage/ahci.rs` - SATA storage driver
- `usb/xhci.rs` - USB 3.0 host controller

---

## C++ Kernel Components

### Introduction

C++ can be used for **specific kernel components** where object-oriented design or templates provide clear benefits. However, C++ use is **highly restricted** and **optional**.

### Why C++ (Limited Use)?

✅ **RAII**: Automatic resource management  
✅ **Templates**: Generic data structures  
✅ **OOP**: Complex state machines  
⚠️ **Restrictions**: No exceptions, no RTTI, freestanding only

### When to Use C++

**✅ Appropriate:**
- Advanced schedulers (template-based)
- Network protocol stacks (state machines)
- Complex filesystem implementations

**❌ Inappropriate:**
- Memory management primitives
- Interrupt handlers
- Boot code
- Architecture-specific code
- Simple drivers

### Project Structure

```
ir0-kernel/
├── cpp/
│   ├── include/
│   │   └── compat.h       # C++ compatibility
│   └── runtime/
│       └── compat.cpp     # C++ runtime
├── kernel/
│   ├── scheduler/
│   │   └── cfs_cpp.cpp   # C++ scheduler (example)
│   └── net/
│       └── tcp_stack.cpp # C++ network stack
└── Makefile
```

### Setting Up C++ Environment

1. **Install G++**:
```bash
sudo apt-get install g++
```

2. **Verify installation**:
```bash
g++ --version
```

### C++ Restrictions in Kernel

**Compiler Flags (Required):**
```bash
-fno-exceptions      # No C++ exceptions
-fno-rtti           # No runtime type information
-ffreestanding      # Freestanding environment
-nostdlib           # No standard library
-fno-threadsafe-statics  # No thread-safe statics
```

**Language Features:**

| Feature | Allowed | Notes |
|---------|---------|-------|
| Classes | ✅ Yes | Core OOP |
| Templates | ✅ Yes | Generic programming |
| Constructors/Destructors | ✅ Yes | RAII |
| Operator Overloading | ✅ Yes | Custom types |
| Exceptions | ❌ No | -fno-exceptions |
| RTTI | ❌ No | -fno-rtti |
| STL | ❌ No | Freestanding |
| `<iostream>` | ❌ No | No I/O streams |
| `new`/`delete` | ✅ Custom | Use kernel allocator |

### Writing C++ Kernel Components

#### Step 1: Create Component File

Create `kernel/scheduler/advanced_scheduler.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — C++ Component
 * Copyright (C) 2025  Your Name
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 *
 * File: advanced_scheduler.cpp
 * Description: Template-based advanced scheduler
 */

#include <cpp/include/compat.h>

extern "C" {
    #include <ir0/kmem.h>
    #include <ir0/print.h>
    #include <ir0/critical.h>
}

namespace ir0 {
namespace scheduler {

/// Template-based task queue
template<typename T, size_t Size>
class TaskQueue {
private:
    T buffer[Size];
    size_t head;
    size_t tail;
    size_t count;

public:
    TaskQueue() : head(0), tail(0), count(0) {}
    
    bool enqueue(const T& item) {
        if (count >= Size) return false;
        
        buffer[tail] = item;
        tail = (tail + 1) % Size;
        count++;
        return true;
    }
    
    bool dequeue(T& item) {
        if (count == 0) return false;
        
        item = buffer[head];
        head = (head + 1) % Size;
        count--;
        return true;
    }
    
    size_t size() const { return count; }
    bool empty() const { return count == 0; }
    bool full() const { return count >= Size; }
};

/// Advanced scheduler class
class AdvancedScheduler {
private:
    TaskQueue<void*, 256> ready_queue;
    bool initialized;

public:
    AdvancedScheduler() : initialized(false) {}
    
    ~AdvancedScheduler() {
        // Cleanup
    }
    
    void init() {
        if (initialized) return;
        
        print("Advanced C++ scheduler initializing...\n");
        initialized = true;
        print_success("Advanced scheduler initialized\n");
    }
    
    void add_task(void* task) {
        CHECK_PTR(task, "add_task");
        
        if (!ready_queue.enqueue(task)) {
            print_error("Task queue full!\n");
        }
    }
    
    void* pick_next_task() {
        void* task = nullptr;
        
        if (ready_queue.dequeue(task)) {
            return task;
        }
        
        return nullptr;  // No tasks
    }
    
    size_t task_count() const {
        return ready_queue.size();
    }
};

// Global scheduler instance
static AdvancedScheduler* g_scheduler = nullptr;

} // namespace scheduler
} // namespace ir0

// C interface for kernel
extern "C" {

void advanced_scheduler_init() {
    using namespace ir0::scheduler;
    
    // Allocate scheduler using kernel allocator
    g_scheduler = new AdvancedScheduler();
    g_scheduler->init();
}

void advanced_scheduler_add_task(void* task) {
    if (ir0::scheduler::g_scheduler) {
        ir0::scheduler::g_scheduler->add_task(task);
    }
}

void* advanced_scheduler_pick_next() {
    if (ir0::scheduler::g_scheduler) {
        return ir0::scheduler::g_scheduler->pick_next_task();
    }
    return nullptr;
}

void advanced_scheduler_cleanup() {
    if (ir0::scheduler::g_scheduler) {
        delete ir0::scheduler::g_scheduler;
        ir0::scheduler::g_scheduler = nullptr;
    }
}

} // extern "C"
```

#### Step 2: Build the Component

```bash
make unibuild -cpp kernel/scheduler/advanced_scheduler.cpp
```

### C++ Best Practices for Kernel

#### RAII (Resource Acquisition Is Initialization)

```cpp
class ResourceGuard {
private:
    void* resource;
    
public:
    ResourceGuard(size_t size) {
        resource = kmalloc(size);
        if (!resource) {
            kernel_panic("Allocation failed");
        }
    }
    
    ~ResourceGuard() {
        if (resource) {
            kfree(resource);
        }
    }
    
    void* get() { return resource; }
};

// Usage - automatically freed when scope exits
{
    ResourceGuard guard(1024);
    void* ptr = guard.get();
    // Use ptr...
} // Automatically freed here
```

#### Templates for Type Safety

```cpp
template<typename T>
class TypedBuffer {
private:
    T* data;
    size_t capacity;
    
public:
    TypedBuffer(size_t size) 
        : capacity(size) {
        data = static_cast<T*>(kmalloc(size * sizeof(T)));
    }
    
    ~TypedBuffer() {
        if (data) kfree(data);
    }
    
    T& operator[](size_t index) {
        CHECK_BOUNDS(index, capacity, "TypedBuffer");
        return data[index];
    }
};
```

#### Namespace Organization

```cpp
namespace ir0 {
namespace kernel {
namespace memory {

class Allocator {
    // Implementation
};

} // namespace memory
} // namespace kernel
} // namespace ir0
```

### C++ Component Examples

See `kernel/` for examples:
- `scheduler/cfs_cpp.cpp` - C++ CFS scheduler
- `net/tcp_stack.cpp` - TCP/IP stack
- `fs/ext2_cpp.cpp` - EXT2 filesystem

---

## Code Style Guidelines

### C Code Style

```c
// Function naming: snake_case
void my_function_name(void) {
    // Variable naming: snake_case
    int my_variable = 0;
    
    // Constants: UPPER_SNAKE_CASE
    const int MAX_SIZE = 1024;
    
    // Pointer style: type* name (star with type)
    void* ptr = kmalloc(size);
    
    // Braces: K&R style
    if (condition) {
        // code
    } else {
        // code
    }
}
```

### Rust Code Style

```rust
// Follow Rust conventions
fn my_function_name() -> Result<(), i32> {
    // Use snake_case for variables
    let my_variable = 0;
    
    // Use SCREAMING_SNAKE_CASE for constants
    const MAX_SIZE: usize = 1024;
    
    // Use match for error handling
    match operation() {
        Ok(value) => Ok(()),
        Err(e) => Err(e),
    }
}
```

### C++ Code Style

```cpp
// Classes: PascalCase
class MyClassName {
private:
    // Members: snake_case with trailing underscore
    int member_variable_;
    
public:
    // Methods: snake_case
    void my_method() {
        // Local variables: snake_case
        int local_var = 0;
    }
};

// Namespaces: lowercase
namespace ir0 {
namespace subsystem {
    // code
}
}
```

### File Headers

All source files must include a GPL-3.0 header:

```c
// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: filename.c
 * Description: Brief file description
 */
```

---

## Testing Guidelines

### Build Testing

```bash
# Test all configurations
make deptest

# Test C compilation
make unibuild kernel/process.c

# Test Rust compilation
make unibuild -rust rust/drivers/test.rs

# Test C++ compilation
make unibuild -cpp kernel/component.cpp

# Test cross-compilation
make unibuild -win kernel/module.c
```

### Runtime Testing

```bash
# Run in QEMU
make run

# Run with menuconfig
make menuconfig
```

---

## Resources

- **Documentation**: `/docs/LANGUAGE_SUPPORT_ROADMAP.md`
- **Rust FFI**: `/rust/ffi/kernel.rs`
- **C++ Compat**: `/cpp/include/compat.h`
- **Examples**: `/rust/drivers/` and `/kernel/`

---

*This guide is a living document. Contributions welcome!*
