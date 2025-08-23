# IR0 Kernel

If you see some "interesting" comments around this docs make me know it in <ivanrwcm25@gmail.com>. i generate docs with ai :--)

## 🚀 Current Status (v0.0.1 pre-rc)

- You can also watch out the IR0 wiki <https://ir0-wiki.netlify.app/>.

**STABLE & FUNCTIONAL** - The kernel compiles and runs successfully on both x86-32 and x86-64 architectures. Core subsystems are operational with a solid foundation for further development.

### ✅ Working Components
- **✅ Dual Architecture Support**: x86-32 and x86-64 compilation and boot
- **✅ Memory Management**: Basic paging, heap allocator, physical memory management
- **✅ Interrupt System**: IDT setup, basic ISR handlers, TSS for x86-64
- **✅ Process Management**: Process structure and state management framework
- **✅ System Calls Interface**: POSIX-compatible system call definitions
- **✅ Interactive Shell**: Basic shell with command parsing and execution
- **✅ Context Switching**: Assembly implementation for both architectures
- **✅ Timer System**: PIT timer support (currently active)
- **✅ String Library**: Complete freestanding implementation

## 🏗️ Architecture

### Supported Architectures
- **x86-32** (32-bit) - ✅ Compiles and boots
- **x86-64** (64-bit) - ✅ Compiles and boots

### Core Subsystems
- **Memory Management** - Basic paging, heap allocator, physical memory management
- **Interrupt System** - IDT setup, ISR handlers, TSS configuration
- **File System** - VFS framework with IR0FS filesystem structure
- **Process Management** - Process lifecycle framework with scheduler interface
- **System Calls** - POSIX-compatible interface definitions
- **Interactive Shell** - Basic command-line interface
- **Timer Drivers** - PIT (active), HPET and LAPIC support structure

## 📁 Project Structure

```
ir0-kernel/
├── arch/                    # Architecture-specific code
│   ├── x86-32/             # 32-bit x86 implementation
│   ├── x86-64/             # 64-bit x86 implementation
│   └── common/              # Common architecture code
├── kernel/                  # Core kernel components
│   ├── core/               # Kernel core functionality
│   ├── process/            # Process management
│   ├── scheduler/          # Task scheduling framework
│   ├── syscalls/           # System call interface
│   └── shell/              # Interactive shell
├── memory/                 # Memory management
├── fs/                     # File system (VFS + IR0FS)
├── drivers/                # Hardware drivers
├── interrupt/              # Interrupt handling
├── includes/               # Header files and utilities
└── setup/                  # Kernel configuration
```

## 🔧 Building and Running

### Prerequisites
- GCC with multilib support
- NASM assembler
- QEMU for testing
- Make

### Quick Build
```bash
# Build for x86-64 desktop
./scripts/build_simple.sh -a x86-64 -t desktop -r -l

# Build for x86-32 desktop
./scripts/build_simple.sh -a x86-32 -t desktop -r -l
```

### Manual Build
```bash
# Clean and build
make clean-all
make ARCH=x86-64 BUILD_TARGET=desktop

# Run in QEMU
make ARCH=x86-64 BUILD_TARGET=desktop run
```

## 🎯 Features

### Memory Management
- **Basic Paging** with identity mapping
- **Heap Allocator** (kmalloc/kfree) with basic fragmentation management
- **Physical Memory Management** with bitmap-based allocation
- **Memory Layout** with kernel and user space separation

### Process Management Framework
- **Process States**: NEW, READY, RUNNING, SLEEPING, STOPPED, ZOMBIE, DEAD
- **Scheduler Interface**: Framework for multiple scheduling algorithms
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
- **Kernel Compilation**: Both x86-32 and x86-64 architectures
- **Memory Management**: Basic memory management with identity mapping
- **Interrupt Handling**: IDT setup with basic exception handling
- **Timer System**: PIT timer active
- **VFS Framework**: Basic filesystem abstraction layer
- **Process Management**: Process structure and state management
- **System Calls Interface**: POSIX-compatible definitions
- **Interactive Shell**: Basic command-line interface
- **Context Switching**: Assembly implementation for both architectures
- **String Functions**: Complete freestanding library

### 🎯 Development Status
- **Foundation Complete**: Core kernel structure and basic functionality
- **Architecture Support**: Both x86-32 and x86-64 working
- **Memory Management**: Basic but functional memory system
- **Interrupt System**: Basic interrupt handling operational

### ⚠️ Areas for Development
- **File System Implementation**: IR0FS low-level functions need implementation
- **Process Creation**: Real process spawning and execution
- **System Call Handlers**: Specific implementations for system calls
- **Keyboard Input**: For full shell interactivity
- **Device Drivers**: Additional hardware support
- **Networking**: Network stack implementation

## 🎯 Highlights

### Technical Achievements
- **Dual Architecture Support**: Native support for both 32-bit and 64-bit
- **Memory Management**: Functional paging and heap allocation
- **Interrupt System**: Complete IDT with basic exception handling
- **Context Switching**: Real assembly implementation for both architectures
- **Freestanding Environment**: Complete C library implementation

### Development Approach
- **Modular Architecture**: Clear separation of concerns
- **Clean Code Organization**: Consistent patterns and structure
- **Extensive Documentation**: Detailed subsystem documentation
- **Robust Foundation**: Solid base for future development

## 🚀 Next Steps (Development Roadmap)

### Immediate Priorities
1. **🎯 File System Implementation** (IR0FS low-level functions)
2. **🎯 Process Creation** (real process spawning and management)
3. **🎯 System Call Handlers** (complete implementations)
4. **🎯 Keyboard Input** (for complete shell interactivity)

### Future Goals
- **Networking Stack**: TCP/IP implementation
- **Additional Device Drivers**: USB, PCI, SATA
- **GUI Framework**: Basic graphical interface
- **Container Support**: Process isolation and namespaces
- **ARM Architecture Support**: AArch64 and ARM32

## 🔍 Development

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

## 📚 Documentation

- **DEVELOPER_GUIDE.md** - Development guidelines and patterns
- **BUILD_SYSTEM.md** - Build system documentation
- **INSTALL.md** - Installation and setup instructions
- **Subsystem READMEs** - Detailed documentation for each subsystem

## 🤝 Contributing

The project welcomes contributions! Please see the development guidelines and ensure your code follows the established patterns. This is a development kernel with a solid foundation for further development.

## 📄 License

This project is licensed under the terms of GLP V 3 LICENSE file.

---

**IR0 Kernel** - *just for fun* 


