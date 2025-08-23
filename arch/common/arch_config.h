// ===============================================================================
// IR0 KERNEL ARCHITECTURE CONFIGURATION
// ===============================================================================
// This file provides architecture detection and configuration for portability
// Supports: x86-32, x86-64, ARM-32, ARM-64 (future)

#pragma once

// ===============================================================================
// ARCHITECTURE DETECTION
// ===============================================================================

#if defined(__x86_64__) || defined(__amd64__)
    #define ARCH_X86_64
    #define ARCH_NAME "x86-64"
    #define ARCH_BITS 64
    #define ARCH_LITTLE_ENDIAN 1
    #define ARCH_HAS_MMU 1
    #define ARCH_HAS_FPU 1
    #define ARCH_HAS_SIMD 1
    #define ARCH_IO_METHOD "port_io"
    #define ARCH_INTERRUPT_METHOD "idt"
    #define ARCH_PAGING_METHOD "x86_paging"
    
#elif defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__)
    #define ARCH_X86_32
    #define ARCH_NAME "x86-32"
    #define ARCH_BITS 32
    #define ARCH_LITTLE_ENDIAN 1
    #define ARCH_HAS_MMU 1
    #define ARCH_HAS_FPU 1
    #define ARCH_HAS_SIMD 0
    #define ARCH_IO_METHOD "port_io"
    #define ARCH_INTERRUPT_METHOD "idt"
    #define ARCH_PAGING_METHOD "x86_paging"
    
#elif defined(__aarch64__)
    #define ARCH_ARM64
    #define ARCH_NAME "ARM-64"
    #define ARCH_BITS 64
    #define ARCH_LITTLE_ENDIAN 1
    #define ARCH_HAS_MMU 1
    #define ARCH_HAS_FPU 1
    #define ARCH_HAS_SIMD 1
    #define ARCH_IO_METHOD "mmio"
    #define ARCH_INTERRUPT_METHOD "gic"
    #define ARCH_PAGING_METHOD "arm_paging"
    
#elif defined(__arm__)
    #define ARCH_ARM32
    #define ARCH_NAME "ARM-32"
    #define ARCH_BITS 32
    #define ARCH_LITTLE_ENDIAN 1
    #define ARCH_HAS_MMU 1
    #define ARCH_HAS_FPU 1
    #define ARCH_HAS_SIMD 0
    #define ARCH_IO_METHOD "mmio"
    #define ARCH_INTERRUPT_METHOD "gic"
    #define ARCH_PAGING_METHOD "arm_paging"
    
#else
    #error "Arquitectura no soportada"
#endif

// ===============================================================================
// ARCHITECTURE-SPECIFIC FEATURES
// ===============================================================================

// Memory alignment requirements
#ifdef ARCH_X86_64
    #define ARCH_ALIGNMENT 8
    #define ARCH_PAGE_SIZE 4096
    #define ARCH_STACK_ALIGNMENT 16
#elif defined(ARCH_X86_32)
    #define ARCH_ALIGNMENT 4
    #define ARCH_PAGE_SIZE 4096
    #define ARCH_STACK_ALIGNMENT 4
#elif defined(ARCH_ARM64)
    #define ARCH_ALIGNMENT 8
    #define ARCH_PAGE_SIZE 4096
    #define ARCH_STACK_ALIGNMENT 16
#elif defined(ARCH_ARM32)
    #define ARCH_ALIGNMENT 4
    #define ARCH_PAGE_SIZE 4096
    #define ARCH_STACK_ALIGNMENT 8
#endif

// ===============================================================================
// COMPILER-SPECIFIC CONFIGURATION
// ===============================================================================

#ifdef ARCH_X86_64
    #define ARCH_CFLAGS "-m64 -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2"
    #define ARCH_LDFLAGS "-m elf_x86_64"
    #define ARCH_ASM_FORMAT "elf64"
#elif defined(ARCH_X86_32)
    #define ARCH_CFLAGS "-m32 -march=i386 -mno-mmx -mno-sse -mno-sse2"
    #define ARCH_LDFLAGS "-m elf_i386"
    #define ARCH_ASM_FORMAT "elf32"
#elif defined(ARCH_ARM64)
    #define ARCH_CFLAGS "-march=armv8-a -mcpu=cortex-a53"
    #define ARCH_LDFLAGS "-m aarch64elf"
    #define ARCH_ASM_FORMAT "elf64"
#elif defined(ARCH_ARM32)
    #define ARCH_CFLAGS "-march=armv7-a -mcpu=cortex-a7 -mfpu=neon-vfpv4"
    #define ARCH_LDFLAGS "-m armelf"
    #define ARCH_ASM_FORMAT "elf32"
#endif

// ===============================================================================
// SUBSYSTEM COMPATIBILITY
// ===============================================================================

// Define which subsystems are compatible with each architecture
#ifdef ARCH_X86_64
    #define ARCH_SUPPORTS_PCI 1
    #define ARCH_SUPPORTS_ACPI 1
    #define ARCH_SUPPORTS_APIC 1
    #define ARCH_SUPPORTS_HPET 1
    #define ARCH_SUPPORTS_PS2 1
    #define ARCH_SUPPORTS_ATA 1
    #define ARCH_SUPPORTS_VGA 1
    #define ARCH_SUPPORTS_ELF 1
    #define ARCH_SUPPORTS_MULTIBOOT 1
    
#elif defined(ARCH_X86_32)
    #define ARCH_SUPPORTS_PCI 1
    #define ARCH_SUPPORTS_ACPI 0
    #define ARCH_SUPPORTS_APIC 0
    #define ARCH_SUPPORTS_HPET 0
    #define ARCH_SUPPORTS_PS2 1
    #define ARCH_SUPPORTS_ATA 1
    #define ARCH_SUPPORTS_VGA 1
    #define ARCH_SUPPORTS_ELF 1
    #define ARCH_SUPPORTS_MULTIBOOT 1
    
#elif defined(ARCH_ARM64)
    #define ARCH_SUPPORTS_PCI 1
    #define ARCH_SUPPORTS_ACPI 1
    #define ARCH_SUPPORTS_APIC 0
    #define ARCH_SUPPORTS_HPET 0
    #define ARCH_SUPPORTS_PS2 0
    #define ARCH_SUPPORTS_ATA 0
    #define ARCH_SUPPORTS_VGA 0
    #define ARCH_SUPPORTS_ELF 1
    #define ARCH_SUPPORTS_MULTIBOOT 0
    
#elif defined(ARCH_ARM32)
    #define ARCH_SUPPORTS_PCI 0
    #define ARCH_SUPPORTS_ACPI 0
    #define ARCH_SUPPORTS_APIC 0
    #define ARCH_SUPPORTS_HPET 0
    #define ARCH_SUPPORTS_PS2 0
    #define ARCH_SUPPORTS_ATA 0
    #define ARCH_SUPPORTS_VGA 0
    #define ARCH_SUPPORTS_ELF 1
    #define ARCH_SUPPORTS_MULTIBOOT 0
#endif

// ===============================================================================
// DRIVER MAPPING
// ===============================================================================

// Map generic driver names to architecture-specific implementations
#ifdef ARCH_X86_64
    #define ARCH_KEYBOARD_DRIVER "ps2_keyboard"
    #define ARCH_DISPLAY_DRIVER "vga_display"
    #define ARCH_STORAGE_DRIVER "ata_storage"
    #define ARCH_TIMER_DRIVER "pit_timer"
    #define ARCH_INTERRUPT_DRIVER "apic_interrupt"
    
#elif defined(ARCH_X86_32)
    #define ARCH_KEYBOARD_DRIVER "ps2_keyboard"
    #define ARCH_DISPLAY_DRIVER "vga_display"
    #define ARCH_STORAGE_DRIVER "ata_storage"
    #define ARCH_TIMER_DRIVER "pit_timer"
    #define ARCH_INTERRUPT_DRIVER "pic_interrupt"
    
#elif defined(ARCH_ARM64)
    #define ARCH_KEYBOARD_DRIVER "uart_keyboard"
    #define ARCH_DISPLAY_DRIVER "framebuffer_display"
    #define ARCH_STORAGE_DRIVER "mmc_storage"
    #define ARCH_TIMER_DRIVER "arm_timer"
    #define ARCH_INTERRUPT_DRIVER "gic_interrupt"
    
#elif defined(ARCH_ARM32)
    #define ARCH_KEYBOARD_DRIVER "uart_keyboard"
    #define ARCH_DISPLAY_DRIVER "framebuffer_display"
    #define ARCH_STORAGE_DRIVER "mmc_storage"
    #define ARCH_TIMER_DRIVER "arm_timer"
    #define ARCH_INTERRUPT_DRIVER "gic_interrupt"
#endif

// ===============================================================================
// BOOTLOADER CONFIGURATION
// ===============================================================================

#ifdef ARCH_X86_64
    #define ARCH_BOOTLOADER "grub"
    #define ARCH_BOOT_METHOD "multiboot2"
    #define ARCH_ENTRY_POINT "_start"
    
#elif defined(ARCH_X86_32)
    #define ARCH_BOOTLOADER "grub"
    #define ARCH_BOOT_METHOD "multiboot"
    #define ARCH_ENTRY_POINT "_start"
    
#elif defined(ARCH_ARM64)
    #define ARCH_BOOTLOADER "uboot"
    #define ARCH_BOOT_METHOD "device_tree"
    #define ARCH_ENTRY_POINT "_start"
    
#elif defined(ARCH_ARM32)
    #define ARCH_BOOTLOADER "uboot"
    #define ARCH_BOOT_METHOD "device_tree"
    #define ARCH_ENTRY_POINT "_start"
#endif
