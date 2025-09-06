# IR0 Kernel

If you see some "interesting" comments around this docs make me know it in <ivanrwcm25@gmail.com>. i generate docs with ai :--)

## 🚀 Current Status (v0.0.1 pre-rc)

- You can also watch our Discord Server <https://discord.gg/XZ84aPXtxh>.


- The kernel compiles and runs successfully x86-64 architectures.

### ✅ Working Components
- **✅ Dual Architecture Support**: x86-32 and x86-64 compilation and boot
- **✅ Memory Management**: Basic paging, heap allocator, physical memory management
- **✅ Interrupt System**: Complete IDT setup, PIC configuration, ISR handlers, TSS for x86-64
- **✅ Process Management**: Process structure and state management framework with multiple schedulers
- **✅ System Calls Interface**: POSIX-compatible system call definitions
- **✅ Interactive Shell**: Basic shell with command parsing and execution
- **✅ Context Switching**: Assembly implementation for both architectures
- **✅ Timer System**: PIT, HPET, and LAPIC timer support
- **✅ Hardware Drivers**: PS/2 keyboard, ATA storage, VGA display
- **✅ String Library**: Complete freestanding implementation

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

## 📁 Setup


### Common Commands
```bash
make help                    # Show all available commands == RECOMENDED ==
make create-disk             # Create virtual disk with filesystem == RECOMENDED ==
make run-64                  # Build and run x86-64 with GUI
make run-32                  # Build and run x86-32 with GUI
make run-64-nographic        # Build and run x86-64 in terminal
make run-32-nographic        # Build and run x86-32 in terminal
make run-64-with-disk        # Run x86-64 with disk support
make run-32-with-disk        # Run x86-32 with disk support
make run-debug               # Run with full debugging
make clean-all               # Clean everything (including ISOs and bin's)
make clean                   # Clean everything
```

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

### Quick Build
```bash
make clean                  # Clean build files
make clean-all              # Clean everything (including ISOs)
```

#### 💾 **Run with Disk Operations**
```bash
make create-disk            # Create virtual disk with filesystem
make run-64-with-disk       # Run x86-64 with disk support == RECOMENDED ==
make run-32-with-disk       # Run x86-32 with disk support
```


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

### ⚠️ Areas for Development
- **File System Implementation**: IR0FS low-level functions need implementation
- **Process Creation**: Real process spawning and execution
- **System Call Handlers**: Specific implementations for system calls
- **Additional Device Drivers**: USB, PCI, SATA support
- **Networking**: Network stack implementation

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

## 🛠️ Development

### Code Quality
- **Modular Architecture** with clear interfaces
- **Clean Code Organization** with consistent patterns
- **Extensive Documentation** with detailed comments
- **Freestanding C Environment** with custom implementations
- **Robust Error Handling** throughout subsystems

### Build System
- **Multi-Architecture Support** with conditional compilation
- **Automatic Dependency Management** with proper Makefiles
- **QEMU Integration** for testing and debugging
- **Debug/Release Modes** with optimization options
- **ISO Generation** for easy distribution

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


