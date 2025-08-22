# IR0 Kernel - ARM Portability Guide

## Overview / Resumen

This guide explains how the IR0 kernel has been prepared for future ARM portability, including both ARM-32 and ARM-64 architectures.

Esta guía explica cómo el kernel IR0 ha sido preparado para portabilidad ARM futura, incluyendo tanto arquitecturas ARM-32 como ARM-64.

## Architecture Support / Soporte de Arquitecturas

### Currently Supported / Actualmente Soportadas
- **x86-32**: Intel/AMD 32-bit processors
- **x86-64**: Intel/AMD 64-bit processors

### Future Support / Soporte Futuro
- **ARM-32**: ARM 32-bit processors (ARMv7-A)
- **ARM-64**: ARM 64-bit processors (ARMv8-A)

## Key Components / Componentes Clave

### 1. Architecture Configuration (`arch/common/arch_config.h`)

This file provides automatic architecture detection and configuration:

```c
// Automatic detection
#if defined(__x86_64__)
    #define ARCH_X86_64
    #define ARCH_NAME "x86-64"
    #define ARCH_BITS 64
    // ... more configuration
#elif defined(__aarch64__)
    #define ARCH_ARM64
    #define ARCH_NAME "ARM-64"
    #define ARCH_BITS 64
    // ... ARM-specific configuration
#endif
```

### 2. Portable Interface (`arch/common/arch_portable.h`)

Provides a unified interface for all architectures:

```c
// Memory management
void arch_memory_init(void);
arch_addr_t arch_alloc_page(void);
int arch_map_page(arch_addr_t virt, arch_addr_t phys, arch_flags_t flags);

// Interrupts
void arch_interrupt_init(void);
void arch_enable_interrupts(void);
int arch_register_irq(arch_irq_t irq, void (*handler)(void));

// I/O operations
uint8_t arch_io_read8(arch_addr_t addr);
void arch_io_write8(arch_addr_t addr, uint8_t value);
```

### 3. Subsystem Compatibility (`setup/subsystem_config.h`)

Validates that enabled subsystems are compatible with the target architecture:

```c
// Architecture compatibility validation
#if ENABLE_PS2_DRIVER && !ARCH_SUPPORTS_PS2
    #error "PS2 driver enabled but not supported on current architecture"
#endif

#if ENABLE_ATA_DRIVER && !ARCH_SUPPORTS_ATA
    #error "ATA driver enabled but not supported on current architecture"
#endif
```

## Driver Mapping / Mapeo de Drivers

### x86 Drivers
- **Keyboard**: PS2 controller
- **Display**: VGA
- **Storage**: ATA/IDE
- **Timer**: PIT, HPET, LAPIC
- **Interrupts**: PIC, APIC

### ARM Drivers (Future)
- **Keyboard**: UART serial
- **Display**: Framebuffer
- **Storage**: MMC/SD cards
- **Timer**: ARM system timer
- **Interrupts**: GIC (Generic Interrupt Controller)

## Build System / Sistema de Compilación

### Architecture Detection
The build system automatically detects the target architecture:

```makefile
# Detect architecture from compiler
ifeq ($(shell $(CC) -dumpmachine | grep -q x86_64 && echo yes),yes)
    ARCH := x86-64
    ARCH_CFLAGS := -m64 -mcmodel=large -mno-red-zone
else ifeq ($(shell $(CC) -dumpmachine | grep -q aarch64 && echo yes),yes)
    ARCH := arm-64
    ARCH_CFLAGS := -march=armv8-a -mcpu=cortex-a53
endif
```

### Compiler Flags
Each architecture has optimized compiler flags:

```makefile
# x86-64
ARCH_CFLAGS := -m64 -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2

# ARM-64
ARCH_CFLAGS := -march=armv8-a -mcpu=cortex-a53

# ARM-32
ARCH_CFLAGS := -march=armv7-a -mcpu=cortex-a7 -mfpu=neon-vfpv4
```

## Directory Structure / Estructura de Directorios

```
arch/
├── common/                    # Common architecture code
│   ├── arch_config.h         # Architecture detection
│   ├── arch_portable.h       # Portable interface
│   └── arch_interface.h      # Legacy interface
├── x86-32/                   # x86 32-bit specific
├── x86-64/                   # x86 64-bit specific
├── arm-32/                   # ARM 32-bit specific (future)
│   ├── asm/
│   ├── sources/
│   └── include/
└── arm-64/                   # ARM 64-bit specific (future)
    ├── asm/
    ├── sources/
    └── include/
```

## Implementation Steps / Pasos de Implementación

### Phase 1: Infrastructure (Completed)
- [x] Architecture detection system
- [x] Portable interface definition
- [x] Subsystem compatibility validation
- [x] Build system preparation

### Phase 2: ARM Implementation (Future)
- [ ] ARM-32 boot assembly
- [ ] ARM-64 boot assembly
- [ ] ARM interrupt handling
- [ ] ARM memory management
- [ ] ARM timer implementation
- [ ] ARM I/O operations

### Phase 3: Driver Porting (Future)
- [ ] UART driver for ARM
- [ ] Framebuffer driver for ARM
- [ ] MMC/SD card driver for ARM
- [ ] ARM system timer driver
- [ ] GIC interrupt controller

## Usage Examples / Ejemplos de Uso

### Compiling for Different Architectures

```bash
# x86-64 (current)
make ARCH=x86-64

# x86-32 (current)
make ARCH=x86-32

# ARM-64 (future)
make ARCH=arm-64

# ARM-32 (future)
make ARCH=arm-32
```

### Using the Portable Interface

```c
#include <arch_portable.h>

void init_system(void) {
    // Initialize architecture-specific components
    arch_early_init();
    arch_memory_init();
    arch_interrupt_init();
    arch_timer_init();
    arch_late_init();
    
    // Enable interrupts
    arch_enable_interrupts();
}

void handle_io(void) {
    // Portable I/O operations
    uint8_t value = arch_io_read8(0x3F8);  // UART on ARM, port on x86
    arch_io_write8(0x3F8, value);
}
```

## Benefits / Beneficios

### 1. Code Reuse
- Common kernel code works across architectures
- Only architecture-specific code needs to be implemented

### 2. Maintainability
- Clear separation between portable and architecture-specific code
- Easy to add new architectures

### 3. Performance
- Architecture-specific optimizations
- Proper compiler flags for each target

### 4. Validation
- Compile-time checks for architecture compatibility
- Prevents incompatible driver combinations

## Future Enhancements / Mejoras Futuras

### 1. Device Tree Support
- ARM systems typically use device trees
- Automatic hardware detection

### 2. Multi-Architecture Builds
- Single build supporting multiple architectures
- Architecture-specific optimizations

### 3. Cross-Compilation
- Build for ARM from x86 host
- Toolchain management

### 4. Emulation Support
- QEMU ARM emulation
- Testing on different architectures

## Troubleshooting / Solución de Problemas

### Common Issues

1. **Architecture Detection Fails**
   - Check compiler target with `gcc -dumpmachine`
   - Verify architecture flags

2. **Incompatible Subsystems**
   - Review subsystem configuration
   - Check architecture compatibility flags

3. **Missing Architecture Files**
   - Ensure all required files exist for target architecture
   - Check directory structure

### Debug Commands

```bash
# Check architecture detection
make help-arch

# Validate configuration
make ARCH=arm-64 validate

# Show build configuration
make ARCH=arm-64 config
```

## Conclusion / Conclusión

The IR0 kernel is now prepared for ARM portability with a robust architecture detection system, portable interfaces, and comprehensive validation. The modular design allows for easy addition of new architectures while maintaining code quality and performance.

El kernel IR0 está ahora preparado para portabilidad ARM con un sistema robusto de detección de arquitectura, interfaces portables y validación comprehensiva. El diseño modular permite la fácil adición de nuevas arquitecturas mientras mantiene la calidad del código y el rendimiento.
