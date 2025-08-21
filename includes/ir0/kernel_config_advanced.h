// ===============================================================================
// IR0 KERNEL ADVANCED CONFIGURATION
// ===============================================================================
// This file shows how to configure different kernel builds
// Usage: Define KERNEL_CONFIG before including kernel_includes.h

#pragma once

// ===============================================================================
// KERNEL BUILD CONFIGURATIONS
// ===============================================================================

// Choose your kernel configuration:
// #define KERNEL_CONFIG_MINIMAL      // Minimal kernel with basic functionality
// #define KERNEL_CONFIG_BASIC        // Basic kernel with drivers
// #define KERNEL_CONFIG_FULL         // Full kernel with all subsystems
// #define KERNEL_CONFIG_DEVELOPMENT  // Development kernel with debugging
// #define KERNEL_CONFIG_CUSTOM       // Custom configuration (define your own)

// Default to BASIC if no configuration is specified
#ifndef KERNEL_CONFIG_MINIMAL
#ifndef KERNEL_CONFIG_BASIC
#ifndef KERNEL_CONFIG_FULL
#ifndef KERNEL_CONFIG_DEVELOPMENT
#ifndef KERNEL_CONFIG_CUSTOM
#define KERNEL_CONFIG_BASIC
#endif
#endif
#endif
#endif
#endif

// ===============================================================================
// MINIMAL KERNEL CONFIGURATION
// ===============================================================================
#ifdef KERNEL_CONFIG_MINIMAL
    // Only essential components
    #define ENABLE_BUMP_ALLOCATOR     1
    #define ENABLE_HEAP_ALLOCATOR     0
    #define ENABLE_PHYSICAL_ALLOCATOR 0
    #define ENABLE_VIRTUAL_MEMORY     0
    #define ENABLE_PROCESS_MANAGEMENT 0
    #define ENABLE_ELF_LOADER         0
    #define ENABLE_SCHEDULER          0
    #define ENABLE_SYSCALLS           0
    #define ENABLE_VFS                0
    #define ENABLE_IR0FS              0
    #define ENABLE_SHELL              0
    #define ENABLE_KEYBOARD_DRIVER    0
    #define ENABLE_ATA_DRIVER         0
    #define ENABLE_PS2_DRIVER         0
    #define ENABLE_TIMER_DRIVERS      1
    #define ENABLE_DEBUGGING          0
    #define ENABLE_LOGGING            1
#endif

// ===============================================================================
// BASIC KERNEL CONFIGURATION
// ===============================================================================
#ifdef KERNEL_CONFIG_BASIC
    // Basic functionality with drivers
    #define ENABLE_BUMP_ALLOCATOR     1
    #define ENABLE_HEAP_ALLOCATOR     0
    #define ENABLE_PHYSICAL_ALLOCATOR 0
    #define ENABLE_VIRTUAL_MEMORY     0
    #define ENABLE_PROCESS_MANAGEMENT 0
    #define ENABLE_ELF_LOADER         0
    #define ENABLE_SCHEDULER          0
    #define ENABLE_SYSCALLS           0
    #define ENABLE_VFS                0
    #define ENABLE_IR0FS              0
    #define ENABLE_SHELL              0
    #define ENABLE_KEYBOARD_DRIVER    1
    #define ENABLE_ATA_DRIVER         1
    #define ENABLE_PS2_DRIVER         1
    #define ENABLE_TIMER_DRIVERS      1
    #define ENABLE_DEBUGGING          0
    #define ENABLE_LOGGING            1
#endif

// ===============================================================================
// FULL KERNEL CONFIGURATION
// ===============================================================================
#ifdef KERNEL_CONFIG_FULL
    // Full kernel with all subsystems
    #define ENABLE_BUMP_ALLOCATOR     1
    #define ENABLE_HEAP_ALLOCATOR     1
    #define ENABLE_PHYSICAL_ALLOCATOR 1
    #define ENABLE_VIRTUAL_MEMORY     1
    #define ENABLE_PROCESS_MANAGEMENT 1
    #define ENABLE_ELF_LOADER         1
    #define ENABLE_SCHEDULER          1
    #define ENABLE_SYSCALLS           1
    #define ENABLE_VFS                1
    #define ENABLE_IR0FS              1
    #define ENABLE_SHELL              1
    #define ENABLE_KEYBOARD_DRIVER    1
    #define ENABLE_ATA_DRIVER         1
    #define ENABLE_PS2_DRIVER         1
    #define ENABLE_TIMER_DRIVERS      1
    #define ENABLE_DEBUGGING          1
    #define ENABLE_LOGGING            1
#endif

// ===============================================================================
// DEVELOPMENT KERNEL CONFIGURATION
// ===============================================================================
#ifdef KERNEL_CONFIG_DEVELOPMENT
    // Development kernel with debugging and testing
    #define ENABLE_BUMP_ALLOCATOR     1
    #define ENABLE_HEAP_ALLOCATOR     0
    #define ENABLE_PHYSICAL_ALLOCATOR 0
    #define ENABLE_VIRTUAL_MEMORY     0
    #define ENABLE_PROCESS_MANAGEMENT 0
    #define ENABLE_ELF_LOADER         0
    #define ENABLE_SCHEDULER          0
    #define ENABLE_SYSCALLS           0
    #define ENABLE_VFS                0
    #define ENABLE_IR0FS              0
    #define ENABLE_SHELL              0
    #define ENABLE_KEYBOARD_DRIVER    1
    #define ENABLE_ATA_DRIVER         1
    #define ENABLE_PS2_DRIVER         1
    #define ENABLE_TIMER_DRIVERS      1
    #define ENABLE_DEBUGGING          1
    #define ENABLE_LOGGING            1
    #define ENABLE_MEMORY_TESTS       1
    #define ENABLE_STRESS_TESTS       1
#endif

// ===============================================================================
// CUSTOM KERNEL CONFIGURATION
// ===============================================================================
#ifdef KERNEL_CONFIG_CUSTOM
    // Define your own configuration here
    // Example: Kernel with scheduler but no file system
    #define ENABLE_BUMP_ALLOCATOR     1
    #define ENABLE_HEAP_ALLOCATOR     1
    #define ENABLE_PHYSICAL_ALLOCATOR 0
    #define ENABLE_VIRTUAL_MEMORY     0
    #define ENABLE_PROCESS_MANAGEMENT 1
    #define ENABLE_ELF_LOADER         1
    #define ENABLE_SCHEDULER          1
    #define ENABLE_SYSCALLS           1
    #define ENABLE_VFS                0
    #define ENABLE_IR0FS              0
    #define ENABLE_SHELL              0
    #define ENABLE_KEYBOARD_DRIVER    1
    #define ENABLE_ATA_DRIVER         1
    #define ENABLE_PS2_DRIVER         1
    #define ENABLE_TIMER_DRIVERS      1
    #define ENABLE_DEBUGGING          1
    #define ENABLE_LOGGING            1
#endif

// ===============================================================================
// FEATURE-SPECIFIC CONFIGURATIONS
// ===============================================================================

// Memory management configurations
#ifdef ENABLE_MEMORY_TESTS
    #define MEMORY_TEST_BUMP_ALLOCATOR    1
    #define MEMORY_TEST_HEAP_ALLOCATOR    0
    #define MEMORY_TEST_STRESS_LEVEL      2  // 0=minimal, 1=basic, 2=full
#endif

// Scheduler configurations
#ifdef ENABLE_SCHEDULER
    #define SCHEDULER_TYPE_ROUND_ROBIN    1
    #define SCHEDULER_TYPE_PRIORITY       1
    #define SCHEDULER_TYPE_CFS            1
    #define SCHEDULER_MAX_TASKS           64
    #define SCHEDULER_TIME_SLICE          10  // milliseconds
#endif

// File system configurations
#ifdef ENABLE_VFS
    #define VFS_MAX_MOUNTS               8
    #define VFS_MAX_OPEN_FILES           256
    #define VFS_CACHE_SIZE               1024  // KB
#endif

// Debugging configurations
#ifdef ENABLE_DEBUGGING
    #define DEBUG_LEVEL                  2  // 0=off, 1=errors, 2=warnings, 3=info, 4=debug
    #define DEBUG_MEMORY                 1
    #define DEBUG_SCHEDULER              1
    #define DEBUG_FILESYSTEM             1
    #define DEBUG_DRIVERS                1
#endif

// ===============================================================================
// BUILD INFORMATION
// ===============================================================================

// Build type information
#ifdef KERNEL_CONFIG_MINIMAL
    #define KERNEL_BUILD_TYPE "MINIMAL"
#elif defined(KERNEL_CONFIG_BASIC)
    #define KERNEL_BUILD_TYPE "BASIC"
#elif defined(KERNEL_CONFIG_FULL)
    #define KERNEL_BUILD_TYPE "FULL"
#elif defined(KERNEL_CONFIG_DEVELOPMENT)
    #define KERNEL_BUILD_TYPE "DEVELOPMENT"
#elif defined(KERNEL_CONFIG_CUSTOM)
    #define KERNEL_BUILD_TYPE "CUSTOM"
#else
    #define KERNEL_BUILD_TYPE "UNKNOWN"
#endif

// Feature summary macros
#define HAS_MEMORY_MANAGEMENT() (ENABLE_BUMP_ALLOCATOR || ENABLE_HEAP_ALLOCATOR || ENABLE_PHYSICAL_ALLOCATOR)
#define HAS_PROCESS_MANAGEMENT() (ENABLE_PROCESS_MANAGEMENT || ENABLE_SCHEDULER)
#define HAS_FILE_SYSTEM() (ENABLE_VFS || ENABLE_IR0FS)
#define HAS_DRIVERS() (ENABLE_KEYBOARD_DRIVER || ENABLE_ATA_DRIVER || ENABLE_PS2_DRIVER)
#define HAS_DEBUGGING() (ENABLE_DEBUGGING || ENABLE_LOGGING)
