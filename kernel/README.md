# IR0 Kernel Core Subsystem

This directory contains the core kernel functionality and main subsystems for IR0.

## Components

### Core Files
- `main.c` - Main kernel initialization and user-space transition routines
- `init.c` - PID 1 init process "init_1" with service management
- `process.c` - Complete process lifecycle management with fork/exec/wait
- `syscalls.c` - 23 implemented system calls with INT 0x80 interface
- `shell.c` - Interactive shell running in Ring 3 user mode
- `elf_loader.c` - Basic ELF binary loader for user programs
- `task.c` - Task management and context switching support

### Scheduler Subsystem (`scheduler/`)
- `cfs_scheduler.c` - Complete CFS (Completely Fair Scheduler) implementation
- `scheduler_detection.c` - Scheduler selection and initialization
- `scheduler_types.h` - Scheduler interfaces and data structures
- `switch/switch_x64.asm` - Optimized x86-64 context switching in assembly

## Features

### Process Management
- ✅ Complete process lifecycle (create, run, terminate)
- ✅ Process states: READY, RUNNING, BLOCKED, ZOMBIE
- ✅ PID assignment starting from PID 1
- ✅ Parent-child relationships with waitpid()
- ✅ Fork() system call (with known context switching issues)
- ✅ Process memory isolation with Ring 0/3 separation

### Scheduler System
- ✅ CFS (Completely Fair Scheduler) with Red-Black Tree
- ✅ O(log n) scheduling complexity
- ✅ Virtual runtime (vruntime) tracking for fairness
- ✅ Nice values support (-20 to +19)
- ✅ Preemptive multitasking with timer integration
- ✅ Assembly-optimized context switching

### System Call Interface
- ✅ 23 implemented syscalls via INT 0x80
- ✅ Process management: fork, exec, exit, wait, getpid
- ✅ File operations: read, write, open, close, ls, cat, mkdir, rm
- ✅ Memory management: brk, sbrk, mmap, munmap, mprotect
- ✅ System info: ps (process list)

### Init System (PID 1)
- ✅ Mini-systemd implementation
- ✅ Service management and respawning
- ✅ Shell service supervision
- ✅ User mode execution (Ring 3)
- ⚠️ Basic zombie reaping (needs improvement)

### Interactive Shell
- ✅ Runs in Ring 3 user mode
- ✅ Built-in commands: ls, ps, cat, mkdir, rmdir, touch, rm, fork, clear, help, exit
- ✅ Memory management testing: malloc, sbrk
- ✅ Process management: fork testing, exec support
- ✅ File system integration
- ✅ Command parsing and execution

### ELF Loader
- ✅ Basic ELF binary loading
- ✅ User program execution support
- ⚠️ Limited functionality (needs enhancement)

## Architecture

### Memory Layout
- Kernel Space: 0x100000 - 0x800000 (1MB-8MB)
- Heap Space: 0x800000 - 0x2000000 (8MB-32MB)
- User Space: 0x40000000+ (1GB+)

### Privilege Levels
- Ring 0: Kernel code, drivers, system calls
- Ring 3: User processes, shell, applications

### Context Switching
- Assembly-optimized x86-64 implementation
- Complete CPU state preservation
- Register and segment management
- Stack switching between kernel/user

## Current Status

### ✅ Stable Components
- Kernel initialization and boot
- Process management core
- CFS scheduler
- System call interface
- Shell and basic commands
- Init system

### ⚠️ Known Issues
- Fork() context switching needs refinement for user/kernel separation
- Basic zombie reaping in init process
- ELF loader needs enhancement
- Limited IPC mechanisms

### 🔄 In Development
- Advanced process features
- Enhanced ELF loading
- Signal handling
- Improved IPC

## Build Integration

The kernel subsystem is built as part of the main kernel binary and includes:
- Automatic dependency tracking
- Debug symbol generation
- Architecture-specific optimizations
- Integration with memory management and drivers