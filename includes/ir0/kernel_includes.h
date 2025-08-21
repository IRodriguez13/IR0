// ===============================================================================
// IR0 KERNEL INCLUDES - Centralized Include System
// ===============================================================================
// This file centralizes all kernel includes and allows easy subsystem activation
// Usage: #include <ir0/kernel_includes.h>

#pragma once

// Include the integrated configuration system
#include <setup/subsystem_config.h>

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
#include <arch/arch_interface.h>
#include <arch/idt.h>

#ifdef __x86_64__
    #include <arch/x86-64/tss.h>
    // #include <arch/x86-64/paging.h>  // TODO: Implement
#else
    // #include <arch/x86-32/paging.h>  // TODO: Implement
#endif

// ===============================================================================
// MEMORY MANAGEMENT INCLUDES
// ===============================================================================
#if ENABLE_BUMP_ALLOCATOR
    #include <memory/bump_allocator.h>
#endif

#if ENABLE_HEAP_ALLOCATOR
    #include <memory/heap_allocator.h>
#endif

#if ENABLE_PHYSICAL_ALLOCATOR
    #include <memory/physical_allocator.h>
#endif

#if ENABLE_VIRTUAL_MEMORY
    #include <memory/virtual_memory.h>
#endif

// ===============================================================================
// INTERRUPT SYSTEM INCLUDES
// ===============================================================================
#include <interrupt/idt.h>
#include <interrupt/pic.h>

#if ENABLE_KEYBOARD_DRIVER
    #include <interrupt/keyboard.h>
#endif

// ===============================================================================
// DRIVER INCLUDES
// ===============================================================================

// Timer drivers
#if ENABLE_TIMER_DRIVERS
    #include <drivers/timer/clock_system.h>
    #include <drivers/timer/pit.h>
    #include <drivers/timer/hpet.h>
    #include <drivers/timer/lapic.h>
#endif

// I/O drivers
#if ENABLE_PS2_DRIVER
    #include <drivers/io/ps2.h>
#endif

// Storage drivers
#if ENABLE_ATA_DRIVER
    #include <drivers/storage/ata.h>
#endif

// Uncomment when implemented:
// #include <drivers/video/vga.h>
// #include <drivers/network/ethernet.h>
// #include <drivers/audio/sound.h>

// ===============================================================================
// FILE SYSTEM INCLUDES
// ===============================================================================
#include <fs/vfs_simple.h>

#if ENABLE_VFS
    #include <fs/vfs.h>
#endif

#if ENABLE_IR0FS
    #include <fs/ir0fs.h>
#endif

// ===============================================================================
// KERNEL SUBSYSTEM INCLUDES
// ===============================================================================

// Process management
#if ENABLE_PROCESS_MANAGEMENT
    #include <kernel/process/process.h>
#endif

#if ENABLE_ELF_LOADER
    #include <kernel/process/elf_loader.h>
#endif

// Scheduler
#if ENABLE_SCHEDULER
    #include <kernel/scheduler/scheduler.h>
    #include <kernel/scheduler/priority_scheduler.h>
    #include <kernel/scheduler/round_robin_scheduler.h>
    #include <kernel/scheduler/cfs_scheduler.h>
#endif

// System calls
#if ENABLE_SYSCALLS
    #include <kernel/syscalls/syscalls.h>
#endif

// Shell
#if ENABLE_SHELL
    #include <kernel/shell/shell.h>
#endif

// ===============================================================================
// CONFIGURATION INCLUDES
// ===============================================================================
#include <setup/kernel_config.h>
