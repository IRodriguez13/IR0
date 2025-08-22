// ===============================================================================
// IR0 KERNEL INCLUDES - Centralized Include System
// ===============================================================================
// This file centralizes all kernel includes and allows easy subsystem activation
// Usage: #include <ir0/kernel_includes.h>

#pragma once

// Include the integrated configuration system
#include <subsystem_config.h>
#include <arch_config.h>

// ===============================================================================
// CORE INCLUDES (Always included)
// ===============================================================================
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <print.h>
#include <logging.h>
#include <panic/panic.h>
#include <validation.h>

// ===============================================================================
// ARCHITECTURE INCLUDES
// ===============================================================================
#include <arch_interface.h>
#include <arch_portable.h>
#include <idt.h>

#ifdef ARCH_X86_64
    // #include <tss_x64.h>  // TODO: Fix include path
    // #include <arch/x86-64/paging.h>  // TODO: Implement
#elif defined(ARCH_X86_32)
    // #include <arch/x86-32/paging.h>  // TODO: Implement
#elif defined(ARCH_ARM64)
    // #include <arch/arm-64/paging.h>  // TODO: Implement
#elif defined(ARCH_ARM32)
    // #include <arch/arm-32/paging.h>  // TODO: Implement
#endif

// ===============================================================================
// MEMORY MANAGEMENT INCLUDES
// ===============================================================================
#if ENABLE_BUMP_ALLOCATOR
    #include <bump_allocator.h>
#endif

#if ENABLE_HEAP_ALLOCATOR
    // #include <heap_allocator.h>  // TODO: Implement
#endif

#if ENABLE_PHYSICAL_ALLOCATOR
    // #include <physical_allocator.h>  // TODO: Implement
#endif

#if ENABLE_VIRTUAL_MEMORY
    // #include <virtual_memory.h>  // TODO: Implement
#endif

#if ENABLE_PAGING
    #include <paging_x64.h>
#endif

// ===============================================================================
// INTERRUPT SYSTEM INCLUDES
// ===============================================================================
#include <idt.h>
#include <pic.h>

#if ENABLE_KEYBOARD_DRIVER
    // #include <keyboard.h>  // TODO: Fix include path
#endif

// ===============================================================================
// DRIVER INCLUDES
// ===============================================================================

// Timer drivers
#if ENABLE_TIMER_DRIVERS
    // #include <clock_system.h>  // TODO: Fix include path
    // #include <pit.h>  // TODO: Fix include path
    // #include <hpet.h>  // TODO: Fix include path
    // #include <lapic.h>  // TODO: Fix include path
#endif

// I/O drivers
#if ENABLE_PS2_DRIVER
    // #include <ps2.h>  // TODO: Fix include path
#endif

// Storage drivers
#if ENABLE_ATA_DRIVER
    // #include <ata.h>  // TODO: Fix include path
#endif

// Uncomment when implemented:
// #include <drivers/video/vga.h>
// #include <drivers/network/ethernet.h>
// #include <drivers/audio/sound.h>

// ===============================================================================
// FILE SYSTEM INCLUDES
// ===============================================================================
// #include <vfs_simple.h>  // TODO: Fix include path

#if ENABLE_VFS
    // #include <vfs.h>  // TODO: Fix include path
#endif

#if ENABLE_IR0FS
    // #include <ir0fs.h>  // TODO: Fix include path
#endif

// ===============================================================================
// KERNEL SUBSYSTEM INCLUDES
// ===============================================================================

// Process management
#if ENABLE_PROCESS_MANAGEMENT
    // #include <process.h>  // TODO: Fix include path
#endif

#if ENABLE_ELF_LOADER
    // #include <elf_loader.h>  // TODO: Fix include path
#endif

// Scheduler
#if ENABLE_SCHEDULER
    // #include <scheduler.h>  // TODO: Fix include path
    // #include <priority_scheduler.h>  // TODO: Fix include path
    // #include <round_robin_scheduler.h>  // TODO: Fix include path
    // #include <cfs_scheduler.h>  // TODO: Fix include path
#endif

// System calls
#if ENABLE_SYSCALLS
    // #include <syscalls.h>  // TODO: Fix include path
#endif

// Shell
#if ENABLE_SHELL
    // #include <shell.h>  // TODO: Fix include path
#endif

// ===============================================================================
// CONFIGURATION INCLUDES
// ===============================================================================
#include <kernel_config.h>
