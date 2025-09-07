# IR0 Kernel - Developer Guide

This guide provides comprehensive information for developers working on the IR0 kernel, including architecture details, coding standards, and development workflows.

---
## Required dependencies to build the kernel

### Build Tools
- **GCC** (GNU Compiler Collection, version 7.0+)
- **Make** (build automation tool)
- **NASM** (Netwide Assembler, version 2.13+)
- **LD** (GNU Linker)
- **AR** (GNU Archiver, usually part of build-essential)

### Bootable image tools
- **GRUB** (grub-pc-bin)
- **Xorriso** (for ISO creation)

### Emulation and testing
- **QEMU** (qemu-system-x86)

### Recommended for development and debugging
- **Git** (version control)
- **Valgrind** (memory debugging, optional)
- **GDB** (debugger, optional)

### Cross-compilation dependencies (optional, only if building for ARM)
- **gcc-aarch64-linux-gnu** (ARM64 cross-compiler)
- **gcc-arm-linux-gnueabi** (ARM32 cross-compiler)
- **qemu-system-arm** (ARM emulator)
---

## 🏗️ Architecture Overview

### Kernel Design Philosophy

The IR0 kernel follows these design principles:

1. **Modularity**: Each subsystem is independent and can be enabled/disabled
2. **Portability**: Architecture-agnostic design with arch-specific implementations
3. **Educational**: Clear code structure for learning OS development
4. **Performance**: Efficient algorithms and data structures
5. **Extensibility**: Easy to add new features and architectures

### Core Subsystems

```
┌─────────────────────────────────────────────────────────────┐
│                    KERNEL ARCHITECTURE                      │
├─────────────────────────────────────────────────────────────┤
│  SCHEDULER  │  MEMORY  │  INTERRUPTS  │  FILESYSTEM  │  DRIVERS │
│             │          │              │              │          │
│ ┌─────────┐ │ ┌──────┐ │ ┌──────────┐ │ ┌─────────┐ │ ┌──────┐ │
│ │ CFS     │ │ │Phys  │ │ │ IDT      │ │ │ VFS     │ │ │VGA   │ │
│ │Priority │ │ │Alloc │ │ │ ISR      │ │ │ Basic   │ │ │Timer │ │
│ │RR       │ │ │Heap  │ │ │ Timer    │ │ │ Ops     │ │ │Storage│ │
│ └─────────┘ │ │VM    │ │ │ Fault    │ │ └─────────┘ │ └──────┘ │
│             │ └──────┘ │ └──────────┘ │              │          │
└─────────────────────────────────────────────────────────────┘
```

---

## 🧠 Scheduler System

### Architecture

The scheduler system uses a plugin architecture with multiple implementations:

```c
typedef struct 
{
    scheduler_type_t type;
    const char *name;
    
    // Function pointers
    void (*init)(void);
    void (*add_task)(task_t *task);
    task_t *(*pick_next_task)(void);
    void (*task_tick)(void);
    void (*cleanup)(void);
    void *private_data;
    
} scheduler_ops_t;
```

### Available Schedulers

#### 1. CFS (Completely Fair Scheduler)
- **File**: `kernel/scheduler/cfs_scheduler.c`
- **Data Structure**: Red-Black Tree
- **Key Features**:
  - Virtual runtime tracking
  - Nice value support (-20 to +19)
  - Load balancing
  - Fair time distribution

#### 2. Priority Scheduler
- **File**: `kernel/scheduler/priority_scheduler.c`
- **Data Structure**: Priority lists with bitmap
- **Key Features**:
  - 140 priority levels
  - Aging to prevent starvation
  - Fast priority selection

#### 3. Round-Robin Scheduler
- **File**: `kernel/scheduler/round-robin_scheduler.c`
- **Data Structure**: Circular linked list
- **Key Features**:
  - Minimal memory usage
  - Equal time slices
  - Simple implementation

### Auto-Detection Logic

The system automatically selects the best scheduler:

```c
scheduler_type_t detect_best_scheduler(void) 
{
    extern uint32_t free_pages_count;
    
    if (free_pages_count > 1000) 
    {
        return SCHEDULER_CFS;        // Most sophisticated
    } 
    else if (free_pages_count > 100) 
    {
        return SCHEDULER_PRIORITY;   // Medium complexity
    } 
    else 
    {
        return SCHEDULER_ROUND_ROBIN; // Fallback
    }
}
```

### Adding a New Scheduler

1. **Create Implementation**:
```c
// my_scheduler.c
static void my_scheduler_init(void) { /* ... */ }
static void my_scheduler_add_task(task_t *task) { /* ... */ }
static task_t *my_scheduler_pick_next_task(void) { /* ... */ }
static void my_scheduler_task_tick(void) { /* ... */ }

scheduler_ops_t my_scheduler_ops = 
{
    .type = SCHEDULER_MY,
    .name = "My Scheduler",
    .init = my_scheduler_init,
    .add_task = my_scheduler_add_task,
    .pick_next_task = my_scheduler_pick_next_task,
    .task_tick = my_scheduler_task_tick,
    .cleanup = NULL,
    .private_data = NULL
};
```

2. **Register in Detection**:
```c
// scheduler_detection.c
extern scheduler_ops_t my_scheduler_ops;

scheduler_type_t detect_best_scheduler(void) 
{
    // Add your detection logic
    if (my_condition) 
    {
        return SCHEDULER_MY;
    }
    // ... existing logic
}
```

---

## 🧠 Memory Management

### Architecture

The memory system is organized in layers:

```
┌─────────────────────────────────────────────────────────────┐
│                    MEMORY MANAGEMENT                        │
├─────────────────────────────────────────────────────────────┤
│                    INTERFACE LAYER                          │
│  kmalloc()  │  kfree()  │  krealloc()  │  vmalloc()  │      │
├─────────────────────────────────────────────────────────────┤
│                    HEAP ALLOCATOR                           │
│  Block Management  │  Fragmentation  │  Statistics  │       │
├─────────────────────────────────────────────────────────────┤
│                  PHYSICAL ALLOCATOR                          │
│  Bitmap Management  │  Page Allocation  │  Memory Zones  │   │
├─────────────────────────────────────────────────────────────┤
│                    PAGING SYSTEM                            │
│  Page Tables  │  TLB Management  │  Fault Handling  │       │
└─────────────────────────────────────────────────────────────┘
```

### Key Components

#### Physical Allocator
- **File**: `memory/physical_allocator.c`
- **Purpose**: Manages physical memory pages
- **Data Structure**: Bitmap
- **Functions**:
  - `alloc_physical_page()`: Allocate 4KB page
  - `free_physical_page()`: Free page
  - `set_page_used()/set_page_free()`: Mark pages

#### Heap Allocator
- **File**: `memory/heap_allocator.c`
- **Purpose**: Kernel memory allocation
- **Data Structure**: Linked list of blocks
- **Functions**:
  - `kmalloc()`: Allocate memory
  - `kfree()`: Free memory
  - `krealloc()`: Resize memory

#### Virtual Memory
- **File**: `memory/ondemand-paging.c`
- **Purpose**: Virtual memory management
- **Features**:
  - On-demand page loading
  - Page fault handling
  - Memory mapping

### Memory Layout

```
Virtual Address Space:
0x00000000 - 0x04000000: Kernel code/data (64MB)
0x04000000 - 0x06000000: Kernel heap (32MB)
0x06000000 - 0x06400000: Kernel stacks (4MB)
0x10000000 - 0x20000000: vmalloc area (256MB)
0x40000000 - 0x80000000: User space (1GB)

Physical Memory:
0x02800000 - 0x08000000: Available physical memory
```

### Adding Memory Features

#### 1. New Memory Zone
```c
// Add to memory_zone_t enum
typedef enum 
{
    ZONE_KERNEL_STATIC,
    ZONE_KERNEL_HEAP,
    ZONE_KERNEL_STACK,
    ZONE_VMALLOC,
    ZONE_USER_SPACE,
    ZONE_MY_NEW_ZONE,  // Add your zone
    ZONE_INVALID
} memory_zone_t;
```

#### 2. New Allocation Function
```c
void *my_alloc(size_t size) 
{
    // Your allocation logic
    uintptr_t phys = alloc_physical_page();
    int result = arch_map_page(virt_addr, phys, flags);
    return (void *)virt_addr;
}
```

---

## ⚡ Interrupt System

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    INTERRUPT SYSTEM                         │
├─────────────────────────────────────────────────────────────┤
│                    IDT (Interrupt Descriptor Table)         │
│  Vector 0-31: CPU Exceptions  │  Vector 32-255: IRQs  │     │
├─────────────────────────────────────────────────────────────┤
│                    ISR HANDLERS                             │
│  Exception Handlers  │  IRQ Handlers  │  Timer Handlers  │  │
├─────────────────────────────────────────────────────────────┤
│                    TIMER SYSTEM                             │
│  PIT  │  HPET  │  LAPIC  │  Best Clock Selection  │        │
└─────────────────────────────────────────────────────────────┘
```

### Key Components

#### IDT Management
- **File**: `interrupt/idt.c`
- **Purpose**: Interrupt descriptor table setup
- **Features**:
  - Exception handling
  - IRQ routing
  - Page fault handling

#### Timer System
- **Files**: `drivers/timer/`
- **Purpose**: System timing and scheduling
- **Components**:
  - PIT: Programmable Interval Timer
  - HPET: High Precision Event Timer
  - LAPIC: Local APIC timer

### Adding Interrupt Handlers

#### 1. Define ISR
```c
// In assembly
global isr_my_handler
isr_my_handler:
    cli
    push eax
    call my_handler_c
    pop eax
    sti
    iret
```

#### 2. Register Handler
```c
// In C
void my_handler_c(void) 
{
    // Handle your interrupt
    LOG_INFO("My interrupt handled");
}

// Register in IDT
void register_my_handler(void) 
{
    idt_set_gate(MY_IRQ_VECTOR, (uint32_t)isr_my_handler, 0x08, 0x8E);
}
```

---

## 🏗️ Build System

### Architecture Support

The build system supports multiple architectures through conditional compilation:

```makefile
ifeq ($(ARCH),x86-64)
    CC = gcc
    CFLAGS = -m64 -mcmodel=large
    ASMFLAGS = -f elf64
    LDFLAGS = -m elf_x86_64
else ifeq ($(ARCH),x86-32)
    CC = gcc
    CFLAGS = -m32 -march=i686
    ASMFLAGS = -f elf32
    LDFLAGS = -m elf_i386
else ifeq ($(ARCH),arm64)
    CC = aarch64-linux-gnu-gcc
    CFLAGS = -march=armv8-a
    ASMFLAGS = --64
    LDFLAGS = -m aarch64linux
endif
```

### Build Targets

Different build targets enable/disable features:

```c
// setup/kernel_config.h
#ifdef IR0_DESKTOP
    #define IR0_ENABLE_GUI 1
    #define IR0_ENABLE_AUDIO 1
    #define IR0_ENABLE_FILESYSTEM 1
#elif defined(IR0_SERVER)
    #define IR0_ENABLE_NETWORKING 1
    #define IR0_ENABLE_FILESYSTEM 1
#elif defined(IR0_IOT)
    #define IR0_ENABLE_POWER_MANAGEMENT 1
    #define IR0_ENABLE_FILESYSTEM 1
#elif defined(IR0_EMBEDDED)
    // Minimal features only
#endif
```

### Adding New Architecture

1. **Create Architecture Directory**:
```bash
mkdir -p arch/myarch/{sources,asm}
mkdir -p memory/arch/myarch
mkdir -p interrupt/arch/myarch
```

2. **Add Build Configuration**:
```makefile
else ifeq ($(ARCH),myarch)
    CC = myarch-linux-gnu-gcc
    CFLAGS = -march=myarch
    ASMFLAGS = -f myarch
    LDFLAGS = -m myarch
    ARCH_SUBDIRS = arch/myarch
    KERNEL_ENTRY = kmain_myarch
```

3. **Implement Architecture Files**:
- `arch/myarch/sources/arch_myarch.c`
- `arch/myarch/asm/boot_myarch.asm`
- `arch/myarch/linker.ld`

---

## 📁 File System

### VFS Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    VIRTUAL FILE SYSTEM                      │
├─────────────────────────────────────────────────────────────┤
│                    VFS INTERFACE                            │
│  vfs_open()  │  vfs_read()  │  vfs_write()  │  vfs_close() │
├─────────────────────────────────────────────────────────────┤
│                    FILE SYSTEM DRIVERS                      │
│  RAMFS  │  EXT2  │  FAT32  │  Custom FS  │                │
└─────────────────────────────────────────────────────────────┘
```

### Current Implementation

- **File**: `fs/vfs_simple.c`
- **Features**:
  - Basic file operations
  - In-memory file system
  - Extensible design

### Adding File System Support

#### 1. Define File System Operations
```c
typedef struct 
{
    const char *name;
    int (*mount)(const char *device, const char *mountpoint);
    int (*open)(const char *path, int flags, vfs_file_t **file);
    int (*read)(vfs_file_t *file, void *buffer, size_t size);
    int (*write)(vfs_file_t *file, const void *buffer, size_t size);
    int (*close)(vfs_file_t *file);
} vfs_ops_t;
```

#### 2. Implement File System
```c
static int myfs_open(const char *path, int flags, vfs_file_t **file) 
{
    // Your file system implementation
    return 0;
}

vfs_ops_t myfs_ops = 
{
    .name = "myfs",
    .open = myfs_open,
    // ... other operations
};
```

#### 3. Register File System
```c
void register_myfs(void) 
{
    vfs_register_filesystem(&myfs_ops);
}
```

---

## 🛠️ Development Workflow

### Setting Up Development Environment

1. **Install Dependencies**:
```bash
sudo apt-get install build-essential nasm grub-pc-bin xorriso qemu-system-x86
```

2. **Clone Repository**:
```bash
git clone https://github.com/your-repo/ir0-kernel.git
cd ir0-kernel
```

3. **Build for Development**:
```bash
make ARCH=x86-64 BUILD_TARGET=desktop
```

### Code Style Guidelines

#### C Code Style
```c
// Function naming: snake_case
void my_function_name(void) 
{
    // Variable naming: snake_case
    int my_variable = 0;
    
    // Constants: UPPER_SNAKE_CASE
    const int MAX_SIZE = 1024;
    
    // Error handling
    if (error_condition) 
    {
        LOG_ERR("Error message");
        return ERROR_CODE;
    }
    
    // Success logging
    LOG_OK("Operation completed successfully");
}
```

#### Comment Style
```c
// Single line comments for simple explanations

/*
 * Multi-line comments for complex explanations
 * Use this for function documentation
 */

/**
 * Function: my_function
 * Purpose: Brief description
 * Parameters:
 *   @param1: Description
 *   @param2: Description
 * Returns: Description of return value
 */
```

#### Error Handling
```c
int my_function(void) 
{
    // Always check for errors
    void *ptr = kmalloc(size);
    if (!ptr) 
    {
        LOG_ERR("Failed to allocate memory");
        return -ENOMEM;
    }
    
    // Use error codes consistently
    if (operation_failed) 
    {
        kfree(ptr);
        return -EINVAL;
    }
    
    return 0; // Success
}
```

### Testing Guidelines

#### 1. Build Testing
```bash
# Test all architectures
make all-arch

# Test all build targets
make all-targets

# Test all combinations
make all-combinations
```

#### 2. Runtime Testing
```bash
# Test in QEMU
make ARCH=x86-64 BUILD_TARGET=desktop run

# Debug mode
make ARCH=x86-32 BUILD_TARGET=desktop debug
```

#### 3. Memory Testing
```c
// Test memory allocation
void *ptr1 = kmalloc(1024);
void *ptr2 = kmalloc(2048);
kfree(ptr1);
void *ptr3 = krealloc(ptr2, 4096);
kfree(ptr3);
```

### Debugging

#### 1. Logging System
```c
// Use appropriate log levels
LOG_INFO("Information message");
LOG_OK("Success message");
LOG_WARN("Warning message");
LOG_ERR("Error message");
```

#### 2. Debug Functions
```c
// Memory debugging
debug_memory_state();

// Scheduler debugging
dump_scheduler_state();

// Architecture debugging
arch_dump_registers();
```

#### 3. QEMU Debugging
```bash
# Run with debug output
make ARCH=x86-64 BUILD_TARGET=desktop debug

# Check debug log
cat qemu_debug.log
```

---

## 🚀 Performance Considerations

### Memory Management
- Use appropriate allocation sizes
- Avoid memory fragmentation
- Profile memory usage

### Scheduler Performance
- Choose appropriate scheduler for workload
- Monitor scheduler overhead
- Optimize context switch time

### Interrupt Handling
- Keep ISRs short
- Use bottom halves for complex processing
- Minimize interrupt latency

---

## 📚 Additional Resources

- **[BUILD_SYSTEM.md](BUILD_SYSTEM.md)**: Complete build system documentation
- **[memory/README](memory/README)**: Memory management details
- **[kernel/scheduler/](kernel/scheduler/)**: Scheduler implementation details
- **OSDev Wiki**: General OS development resources
- **Linux Kernel Documentation**: Reference for design patterns

---

*This guide is a living document. Please contribute improvements and corrections.*
