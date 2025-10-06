# IR0 Kernel Project Structure

## Root Directory Organization

### Core Kernel Components
- **`kernel/`** - Main kernel subsystems (process, scheduler, syscalls, shell)
- **`memory/`** - Memory management (allocator, paging)
- **`interrupt/`** - Interrupt handling system (IDT, PIC, ISR handlers)
- **`fs/`** - Filesystem implementation (VFS, MINIX FS)
- **`drivers/`** - Hardware drivers organized by category

### Architecture Support
- **`arch/`** - Production architecture implementations
  - `arch/common/` - Architecture-agnostic interfaces
  - `arch/x86-64/` - x86-64 specific code (primary target)
- **`arch-wip/`** - Work-in-progress architectures
  - `arch-wip/x86-32/` - 32-bit x86 (experimental)
  - `arch-wip/arm-32/` - ARM 32-bit (in development)

### Support Systems
- **`includes/`** - Header files and freestanding C library
  - `includes/ir0/` - Kernel-specific headers (logging, print, panic)
  - Standard headers (stdint.h, stddef.h, string.h)
- **`setup/`** - Build configuration and kernel config system
- **`userspace/`** - User-mode programs and libc implementation

### Development Infrastructure
- **`scripts/`** - Build and utility scripts
- **`tests/`** - Test programs and validation code
- **`logs/`** - Runtime logs and debug output
- **`backup-old-files/`** - Backup of previous implementations

## Driver Organization

### `drivers/` Structure
- **`IO/`** - Input/Output devices (PS/2 keyboard, mouse)
- **`timer/`** - Timer subsystems (PIT, HPET, LAPIC, RTC)
- **`storage/`** - Storage devices (ATA/IDE)
- **`video/`** - Display drivers (VGA, VBE)
- **`serial/`** - Serial communication
- **`audio/`** - Audio devices (Sound Blaster)

## Architecture-Specific Layout

### `arch/x86-64/` Structure
- **`sources/`** - C implementation files
- **`asm/`** - Assembly language files
- **`linker.ld`** - Linker script
- **`grub.cfg`** - GRUB configuration

### Common Architecture Interface
- **`arch/common/`** - Shared architecture abstractions
  - `arch_interface.h` - Common architecture API
  - `arch_portable.h` - Portable definitions
  - `common_paging.h` - Paging abstractions

## Kernel Subsystem Organization

### `kernel/` Structure
- **Core files**: `kernel.c`, `init.c`, `process.c`, `syscalls.c`
- **`scheduler/`** - Multiple scheduler implementations
  - `switch/` - Context switching assembly code
- **`auth/`** - Authentication system headers
- **`login/`** - Login system implementation

### `memory/` Structure
- **Core**: `allocator.c`, `paging.c`
- **WIP**: `memory-wip/` - Advanced allocators in development

### `interrupt/` Structure
- **`arch/`** - Architecture-specific interrupt code
  - `x86-64/` - 64-bit interrupt stubs
  - `x86-32/` - 32-bit interrupt stubs

## File Naming Conventions

### Source Files
- **C files**: `snake_case.c` (e.g., `clock_system.c`)
- **Headers**: `snake_case.h` (e.g., `arch_interface.h`)
- **Assembly**: `snake_case.asm` (e.g., `boot_x64.asm`)

### Architecture Suffixes
- **x86-64**: `_x64` suffix (e.g., `arch_x64.c`)
- **x86-32**: `_x86` suffix (e.g., `arch_x86.c`)
- **ARM**: `_arm` suffix (e.g., `arch_arm.c`)

### Build Artifacts
- **Object files**: `.o` extension
- **Dependency files**: `.d` extension
- **Binary**: `kernel-x64.bin`
- **ISO**: `kernel-x64.iso`

## Include Path Hierarchy

### Standard Include Order
1. Local headers (relative paths)
2. Kernel headers (`ir0/`)
3. Architecture headers (`arch/`)
4. Standard headers (`stdint.h`, etc.)

### Include Directories (in CFLAGS)
- `$(KERNEL_ROOT)` - Project root
- `$(KERNEL_ROOT)/includes` - Standard library
- `$(KERNEL_ROOT)/includes/ir0` - Kernel headers
- `$(KERNEL_ROOT)/arch/common` - Common arch interface
- `$(KERNEL_ROOT)/arch/x86-64/sources` - x86-64 specific
- Component-specific directories (memory, interrupt, etc.)

## Configuration System

### Build Configuration
- **`setup/kernel_config.h`** - Main configuration header
- **Target-specific**: `IR0_DESKTOP`, `IR0_SERVER`, `IR0_IOT`, `IR0_EMBEDDED`
- **Feature flags**: `IR0_ENABLE_*` macros

### Makefile Structure
- **Architecture detection**: `ARCH` variable
- **Target configuration**: `BUILD_TARGET` variable
- **Conditional compilation**: Feature-based object inclusion
- **QEMU configuration**: Multiple run targets for different scenarios