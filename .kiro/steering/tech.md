# IR0 Kernel Technical Stack

## Build System
- **Primary Build Tool**: GNU Make with comprehensive Makefile
- **Architecture**: x86-64 (primary), x86-32 (experimental), ARM (in progress)
- **Compiler**: GCC with freestanding environment flags
- **Assembler**: NASM for assembly code
- **Linker**: GNU LD with custom linker scripts

## Core Technologies
- **Language**: C (freestanding environment)
- **Assembly**: x86-64 assembly for low-level operations
- **Boot**: GRUB bootloader with multiboot specification
- **Emulation**: QEMU for testing and development

## Dependencies
### Required Build Tools
- GCC (version 7.0+)
- GNU Make
- NASM (version 2.13+)
- GNU LD and AR
- GRUB (grub-pc-bin)
- Xorriso (ISO creation)

### Development Tools
- QEMU (qemu-system-x86)
- GDB (debugging)
- Git (version control)

## Common Build Commands

### Basic Build and Run
```bash
# Create virtual disk (first time only)
make create-disk

# Build kernel ISO
make ir0

# Run with GUI and disk
make run-kernel

# Quick debug mode
make debug
```

### Development Workflow
```bash
# Clean build artifacts
make clean

# Build and run in one step
make ir0 && make run-kernel

# Run without disk for testing
make run-nodisk

# Console mode (no GUI)
make run-console
```

### Debug and Testing
```bash
# Run with debug output and serial logging
make run-debug

# Quick debug with serial output only
make debug

# View QEMU debug log
cat qemu_debug.log
```

## Compiler Configuration
- **Flags**: `-m64 -mcmodel=kernel -mno-red-zone -nostdlib -ffreestanding`
- **Warnings**: `-Wall -Wextra -Werror=implicit-function-declaration`
- **Optimization**: `-O0 -g` (debug builds)
- **Dependencies**: `-MMD -MP` for automatic dependency tracking

## Architecture Support
- **x86-64**: Primary target, fully functional
- **x86-32**: Experimental, needs fixes
- **ARM**: Work in progress in `arch-wip/` directory

## Memory Layout
- **Kernel Space**: 0x100000 - 0x800000 (1MB-8MB)
- **Heap**: 0x800000 - 0x2000000 (8MB-32MB, 24MB total)
- **User Space**: 0x40000000+ (1GB+)

## Key Libraries and Frameworks
- **Custom C Library**: Freestanding implementation in `includes/`
- **Logging System**: Comprehensive logging with multiple levels
- **Memory Management**: Custom allocator with paging support
- **VFS**: Virtual filesystem abstraction layer
- **Scheduler Framework**: Plugin-based scheduler system