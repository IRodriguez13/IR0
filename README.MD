# IR0 Kernel

**IR0** is a custom monolithic kernel written from scratch in C and assembly, designed to explore low-level OS development with a clean and modular architecture. It's inspired by Linux but built for learning and experimentation.

The kernel features a sophisticated multi-scheduler system, comprehensive memory management, and a flexible build system that supports multiple architectures and build targets.

---

## 🚀 Quick Start

### Prerequisites
```bash
# Install dependencies
sudo apt-get install build-essential nasm grub-pc-bin xorriso qemu-system-x86

# For ARM support (optional)
./scripts/install_arm_deps.sh
```

### Build & Run
```bash
# Quick build (x86-32 desktop)
make

# Build for specific architecture and target
make ARCH=x86-64 BUILD_TARGET=desktop

# Build all combinations
./scripts/build_all.sh -c

# Interactive menu
./scripts/menu_builder.sh

# Run in QEMU
make run
```

---

## 🔧 Features

### 🧠 **Advanced Scheduler System**
- **CFS (Completely Fair Scheduler)**: Linux-style fair scheduling with red-black trees
- **Priority Scheduler**: Priority-based scheduling with aging
- **Round-Robin**: Simple fallback scheduler
- **Auto-Detection**: Automatically selects the best scheduler based on available memory

### 🧠 **Comprehensive Memory Management**
- **Physical Memory Allocator**: Bitmap-based page management
- **Kernel Heap**: kmalloc/kfree/krealloc implementation
- **Virtual Memory**: On-demand paging system
- **Memory Zones**: Organized memory layout (kernel, heap, user space)

### ⚡ **Interrupt & Timer System**
- **IDT Management**: Complete interrupt descriptor table
- **ISR Handlers**: Custom interrupt service routines
- **Multi-Timer Support**: PIT, HPET, and LAPIC timers
- **Page Fault Handling**: Sophisticated memory fault management

### 📁 **File System**
- **VFS (Virtual File System)**: Simple but functional file system
- **Basic Operations**: Open, read, write, close
- **Extensible Design**: Easy to add new file system types

### 🏗️ **Architecture Support**
- **x86-32**: Full 32-bit support with protected mode
- **x86-64**: 64-bit long mode support
- **ARM32/ARM64**: Cross-compilation support (infrastructure ready)

### 🎯 **Build Targets**
- **Desktop**: Full-featured system with GUI support
- **Server**: Optimized for server workloads
- **IoT**: Lightweight system for embedded devices
- **Embedded**: Minimal system without file system

---

## 📁 Project Structure

```
ir0-kernel/
├── arch/                    # Architecture-specific code
│   ├── x86-32/            # 32-bit x86 support
│   ├── x86-64/            # 64-bit x86 support
│   └── common/            # Common architecture code
├── kernel/                 # Core kernel subsystems
│   ├── scheduler/         # Multi-scheduler system
│   └── core/              # Kernel initialization
├── memory/                # Memory management
│   ├── heap_allocator.c   # Kernel heap (kmalloc)
│   ├── physical_allocator.c # Physical page management
│   └── ondemand-paging.c  # Virtual memory
├── interrupt/             # Interrupt handling
├── drivers/               # Device drivers
├── fs/                    # File system
├── scripts/               # Build and utility scripts
└── setup/                 # Kernel configuration
```

---

## 🧠 Kernel Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    USER SPACE (Future)                      │
├─────────────────────────────────────────────────────────────┤
│                   KERNEL SPACE (IR0)                        │
├─────────────────────┬─────────────────┬─────────────────────┤
│   SCHEDULER SYSTEM  │  MEMORY SYSTEM  │  INTERRUPT SYSTEM   │
│                     │                 │                     │
│ ┌─────────────────┐ │ ┌─────────────┐ │ ┌─────────────────┐ │
│ │ CFS Scheduler   │ │ │ Physical    │ │ │ IDT Manager     │ │
│ │ Priority Sched  │ │ │ Allocator   │ │ │ ISR Handlers    │ │
│ │ Round Robin     │ │ │             │ │ │ Page Fault      │ │
│ └─────────────────┘ │ ├─────────────┤ │ └─────────────────┘ │
│                     │ │ Heap        │ │                     │
│ ┌─────────────────┐ │ │ Allocator   │ │ ┌─────────────────┐ │
│ │ Task Manager    │ │ │ (kmalloc)   │ │ │ Timer System    │ │
│ │ Context Switch  │ │ │             │ │ │ HPET/LAPIC/PIT  │ │
│ └─────────────────┘ │ ├─────────────┤ │ └─────────────────┘ │
│                     │ │ Virtual     │ │                     │
├─────────────────────┤ │ Allocator   │ ├─────────────────────┤
│                     │ │ (vmalloc)   │ │                     │
│    ARCH LAYER       │ │             │ │    DEVICE DRIVERS   │
│                     │ ├─────────────┤ │                     │
│ ┌─────────────────┐ │ │ On-Demand   │ │ ┌─────────────────┐ │
│ │ x86-32 Support  │ │ │ Paging      │ │ │ VGA Display     │ │
│ │ x86-64 Support  │ │ └─────────────┘ │ │ Storage (TODO)  │ │
│ │ ARM Support     │ │                 │ │ Network (TODO)  │ │
│ └─────────────────┘ └─────────────────┘ └─────────────────┘ │
├─────────────────────────────────────────────────────────────┤
│                    HARDWARE ABSTRACTION                     │
└─────────────────────────────────────────────────────────────┘
```

---

## 🔧 Build System

### Multi-Architecture Support
The kernel supports multiple architectures with conditional compilation:

```bash
# x86 architectures
make ARCH=x86-32 BUILD_TARGET=desktop
make ARCH=x86-64 BUILD_TARGET=server

# ARM architectures (requires cross-compiler)
make ARCH=arm32 BUILD_TARGET=iot
make ARCH=arm64 BUILD_TARGET=embedded
```

### Build Targets
Different build targets enable/disable features:

| Target | Features | Use Case |
|--------|----------|----------|
| **desktop** | GUI, audio, USB, networking, filesystem | Desktop computers |
| **server** | Networking, filesystem, high performance | Server systems |
| **iot** | Minimal features, power management | IoT devices |
| **embedded** | Core only, no filesystem | Embedded systems |

### Build Scripts
```bash
# Build all combinations (16 total)
./scripts/build_all.sh -c

# Build all architectures
./scripts/build_all.sh -a

# Build all targets
./scripts/build_all.sh -t

# Interactive build menu
./scripts/menu_builder.sh

# Create custom ISO
./scripts/create_iso.sh -a x86-64 -t desktop -o my-kernel.iso
```

---

## 🧠 Scheduler System

The kernel features a sophisticated multi-scheduler system inspired by Linux:

### CFS (Completely Fair Scheduler)
- **Red-Black Tree**: Efficient task selection
- **Virtual Runtime**: Fair time distribution
- **Nice Values**: Process priority support
- **Load Balancing**: Automatic load distribution

### Priority Scheduler
- **Priority Lists**: 140 priority levels
- **Aging**: Prevents starvation
- **Bitmap**: Fast priority selection

### Round-Robin Scheduler
- **Simple**: Minimal memory usage
- **Fallback**: Used when memory is limited
- **Fair**: Equal time slices

### Auto-Detection
The system automatically selects the best scheduler:
- **CFS**: When >1000 free pages available
- **Priority**: When >100 free pages available  
- **Round-Robin**: When memory is limited

---

## 🧠 Memory Management

### Physical Memory Allocator
- **Bitmap Management**: Tracks free/used pages
- **Page Allocation**: 4KB page granularity
- **Memory Statistics**: Tracks usage patterns

### Kernel Heap Allocator
- **kmalloc()**: Allocate kernel memory
- **kfree()**: Free kernel memory
- **krealloc()**: Resize memory blocks
- **Fragmentation Management**: Coalesces free blocks

### Virtual Memory System
- **On-Demand Paging**: Load pages when needed
- **Page Fault Handling**: Sophisticated fault management
- **Memory Mapping**: Virtual to physical translation

### Memory Layout
```
0x00000000 - 0x04000000: Kernel code/data (64MB)
0x04000000 - 0x06000000: Kernel heap (32MB)
0x06000000 - 0x06400000: Kernel stacks (4MB)
0x10000000 - 0x20000000: vmalloc area (256MB)
0x40000000 - 0x80000000: User space (1GB)
```

---

## 🚀 Development Status

| Subsystem | Status | Notes |
|-----------|--------|-------|
| 🧼 **Bootloader** | ✅ Complete | Custom bootloader, multi-arch support |
| 🧠 **Scheduler** | ✅ Advanced | CFS, Priority, Round-Robin with auto-detection |
| 📦 **Memory Management** | ✅ Complete | Physical, heap, virtual memory systems |
| ⚡ **Interrupts** | ✅ Complete | IDT, ISRs, page fault handling |
| ⏲ **Timers** | ✅ Complete | PIT, HPET, LAPIC support |
| 📁 **File System** | 🚧 Basic | VFS with basic operations |
| 🏗️ **Build System** | ✅ Complete | Multi-arch, multi-target support |
| 🔀 **User Space** | 🚧 Planned | Process management, syscalls |
| 🖥 **GUI** | 🚧 Planned | Desktop target feature |

---

## 🛠️ Development

### Adding New Features
1. **Architecture Support**: Add to `arch/` directory
2. **Device Drivers**: Add to `drivers/` directory  
3. **Build Targets**: Update `Makefile` and `setup/kernel_config.h`
4. **Schedulers**: Implement `scheduler_ops_t` interface

### Code Style
- **C99 Standard**: Use modern C features
- **Documentation**: Extensive comments in Spanish/English
- **Error Handling**: Comprehensive error checking
- **Memory Safety**: Proper allocation/deallocation

### Testing
```bash
# Build and test all combinations
./scripts/build_all.sh -c

# Test specific architecture
make ARCH=x86-64 BUILD_TARGET=desktop run

# Debug mode
make ARCH=x86-32 BUILD_TARGET=desktop debug
```

---

## 📚 Documentation

- **[BUILD_SYSTEM.md](BUILD_SYSTEM.md)**: Complete build system documentation
- **[memory/README](memory/README)**: Memory management details
- **[kernel/scheduler/](kernel/scheduler/)**: Scheduler implementation details

---

## 🤝 Contributing

This project is designed for learning and experimentation. Contributions are welcome:

1. **Fork** the repository
2. **Create** a feature branch
3. **Implement** your changes
4. **Test** with multiple architectures/targets
5. **Submit** a pull request

### Development Guidelines
- Maintain compatibility with all architectures
- Add comprehensive documentation
- Test with all build targets
- Follow existing code style

---

## 📄 License

This project is open source. See [LICENSE](LICENSE) for details.

---

## 🙏 Acknowledgments

- **Linux Kernel**: Inspiration for scheduler and memory management
- **OSDev Wiki**: Valuable resources for OS development
- **QEMU**: For emulation and testing
- **GCC/GNU**: For cross-compilation support

---

*IR0 Kernel - Where learning meets real-world OS development* 🚀

