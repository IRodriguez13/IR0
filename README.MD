
# IR0 Kernel pre-rc1

IR0 is a monolithic hobby operating system kernel for x86-64, focused on learning, experimentation, and pushing the boundaries of what a homebrew OS can achieve. The project aims to provide a robust, modular, and extensible base for running real-world userland software, including Doom, GCC, Bash, and external TCP/IP networking (ping).

## Project Overview

- **Type:** Monolithic hobby OS kernel
- **Architecture:** x86-64 (x86-32 support is experimental)
- **License:** GNU GPL v3.0

## Current Status

IR0 currently boots on x86-64 hardware and emulators, providing a functional multitasking environment, memory management, basic drivers, and a minimal shell. The kernel is under active development, with the following features implemented:

### Implemented Features

- **x86-64 Boot and Initialization**
- **Memory Management:** Paging, heap allocator, physical memory bitmap
- **Interrupt Handling:** Full IDT, PIC, TSS, and exception support
- **Process Management:** Process structures, state transitions, and multiple schedulers (CFS, priority, round-robin)
- **System Call Interface:** POSIX-like syscall definitions (50+ syscalls)
- **Context Switching:** Assembly implementation for both 32/64-bit
- **Shell:** Minimal interactive shell with command parsing
- **Timer Support:** PIT, HPET, LAPIC
- **Drivers:** PS/2 keyboard, ATA storage, VGA display
- **String/Utility Library:** Freestanding C implementation
- **VFS Layer:** Virtual file system abstraction and IR0FS structure

## Technical Architecture

- **Memory Layout:** Kernel/user separation, identity mapping, protected stack
- **Process Model:** Standard UNIX-like states, process tree, context switch
- **File System:** VFS and IR0FS, basic mounting and directory operations
- **System Calls:** Process, file, memory, and time management
- **Shell:** Built-in commands for system inspection and management

## Build & Run

Requirements: GCC, Make, NASM, LD, AR, GRUB, Xorriso, QEMU

Example:
```bash
create the virtual hard disk
make create-disk
for build & run:
make ir0
make run-kernel
```

## Roadmap & Final Goal

The ultimate goal for IR0 is to:

- Support external TCP/IP networking and utilities like `ping`
- Run Doom natively in userland
- Provide a working GCC toolchain and Bash shell in user space

## Development Status

- **Core:** Stable and functional multitasking, memory, interrupts, and drivers
- **In Progress:** TCP/IP stack, ELF userland loader, advanced file system
- **Next Milestones:** Networking, Doom, Bash, GCC

## Contributing

Contributions are welcome. Please see the developer guide and coding standards in the repository.

## License

This project is licensed under the GNU General Public License v3.0.

---

## 🏗️ Architecture

### Supported Architectures
- **x86-32** (32-bit) -  Needs fixes
- **x86-64** (64-bit) - ✅ Compiles and boots

### Core Subsystems
- **Memory Management** - Basic paging, heap allocator, physical memory management
- **Interrupt System** - Complete IDT setup, PIC configuration, ISR handlers, TSS configuration
- **File System** - VFS framework with IR0FS filesystem structure
- **Process Management** - Process lifecycle framework with multiple scheduler algorithms
- **System Calls** - POSIX-compatible interface definitions
- **Interactive Shell** - Basic command-line interface with authentication
- **Timer Drivers** - PIT, HPET, and LAPIC timer support
- **Hardware Drivers** - PS/2 keyboard, ATA storage, VGA display drivers

## 🔧 Building and Running

### Prerequisites
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


## 🎯 Features

### Memory Management
- **Basic Paging** with identity mapping
- **Heap Allocator** (kmalloc/kfree) with basic fragmentation management
- **Physical Memory Management** with bitmap-based allocation
- **Memory Layout** with kernel and user space separation

### Process Management Framework
- **Process States**: NEW, READY, RUNNING, SLEEPING, STOPPED, ZOMBIE, DEAD
- **Multiple Schedulers**: CFS (Completely Fair Scheduler), Priority-based, Round-Robin
- **Context Switching**: Assembly implementation for both architectures
- **Process Trees**: Hierarchical process management structure

### System Call Interface
- **POSIX-Compatible Definitions**: 50+ system call definitions
- **Process Control**: fork, exec, exit, wait, getpid
- **File Operations**: open, close, read, write, lseek
- **Memory Management**: brk, mmap, munmap, mprotect
- **Time**: time, gettimeofday, sleep, usleep

### Interactive Shell
- **Command Parsing**: Basic command-line interface
- **Built-in Commands**: help, info, version, ps, meminfo
- **System Integration**: Access to kernel structures
- **Authentication System**: Basic user authentication framework
- **Error Handling**: Basic error reporting

### File System Framework
- **VFS Layer**: Virtual File System abstraction
- **IR0FS Structure**: Filesystem layout and metadata definitions
- **Mount Point Management**: Basic filesystem mounting framework

## 🛠️ Technical Specifications

### Memory Layout
- **Kernel Space**: 0x100000 - 0x200000 (1MB)
- **Identity Mapping**: 0x000000 - 0x40000000 (1GB)
- **User Space**: 0x40000000+ (1GB+)
- **Stack**: 0x1000000 with protection

### Context Switching
- **Complete CPU State Preservation**
- **General Purpose Registers** (RAX-R15 for x86-64, EAX-EDI for x86-32)
- **Segment Registers** (CS, DS, ES, FS, GS, SS)
- **Control Registers** (CR0, CR2, CR3, CR4)

## 📊 Current Status

### ✅ Working Components
- **Kernel Compilation**: x86-64 architecture
- **Memory Management**: Basic memory management with identity mapping
- **Interrupt Handling**: Complete IDT setup with PIC configuration and exception handling
- **Timer System**: PIT, HPET, and LAPIC timer support
- **VFS Framework**: Basic filesystem abstraction layer
- **Process Management**: Process structure and state management with multiple schedulers
- **System Calls Interface**: POSIX-compatible definitions
- **Interactive Shell**: Basic command-line interface with authentication
- **Context Switching**: Assembly implementation for both architectures
- **Hardware Drivers**: PS/2 keyboard, ATA storage, VGA display
- **String Functions**: Complete freestanding library

### 🎯 Development Status
- **Foundation Complete**: Core kernel structure and basic functionality
- **Architecture Support**: x86-64 working
- **Memory Management**: Basic but functional memory system
- **Interrupt System**: Complete interrupt handling with PIC and multiple timer support
- **Hardware Support**: PS/2 keyboard, ATA storage, and VGA display drivers operational


## 🎯 Highlights

### Technical Achievements
- **Dual Architecture Support**: Native support for both 32-bit and 64-bit (ARM32 in progress)
- **Memory Management**: Functional paging and heap allocation
- **Interrupt System**: Complete IDT with PIC configuration and multiple timer support
- **Context Switching**: Real assembly implementation for both architectures
- **Hardware Drivers**: PS/2 keyboard, ATA storage, and VGA display support
- **Multiple Schedulers**: CFS, Priority-based, and Round-Robin scheduling algorithms
- **Freestanding Environment**: Complete C library implementation
---

## 📚 Some Documentation

- **[Build System](setup/docsBUILD_SYSTEM.md)** - Build system documentation
- **[Installation](setup/docsINSTALL.md)** - Installation and setup instructions
- **[Configuration](setup/docsCONFIGURATION_SYSTEM_README.md)** - Kernel configuration system
- **[Include System](setup/docsINCLUDE_SYSTEM_GUIDE.md)** - Header file organization

---

## 🤝 Contributing

We welcome contributions! Please see our [Developer Guide](CONTRIBUTING.md) for:

- **Coding Standards** and patterns
- **Architecture Guidelines** for new features
- **Testing Procedures** for changes
- **Documentation Requirements** for new code

### Getting Started
1. Fork the repository
2. Create a 'feat/xx' branch
3. Make your changes following the coding standards
4. Test your changes thoroughly
5. Submit a pull request

---

## 📄 License

This project is licensed under the **GNU General Public License v3.0** - see the [LICENSE](LICENSE) file for details.

---


**IR0 Kernel** 

For feedback, please open an issue or contact me at [ivanrwcm25@gmail.com](mailto:ivanrwcm25@gmail.com). 


