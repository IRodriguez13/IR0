// ===============================================================================
// IR0 KERNEL SUBSYSTEM CONFIGURATION
// ===============================================================================
// This file integrates with the existing strategy system and allows fine-grained
// control over kernel subsystems
// Usage: This works in tandem with kernel_config.h

#pragma once

#include "kernel_config.h"

// ===============================================================================
// SUBSYSTEM ENABLE/DISABLE FLAGS
// ===============================================================================
// Set these to 1 or 0 to enable/disable specific subsystems

// Memory Management Subsystems
#define ENABLE_BUMP_ALLOCATOR     1   // Simple bump allocator (always enabled)
#define ENABLE_HEAP_ALLOCATOR     0   // Dynamic heap allocator
#define ENABLE_PHYSICAL_ALLOCATOR 0   // Physical page allocator
#define ENABLE_VIRTUAL_MEMORY     0   // Virtual memory management

// Process Management Subsystems
#define ENABLE_PROCESS_MANAGEMENT 0   // Process creation and management
#define ENABLE_ELF_LOADER         0   // ELF executable loader
#define ENABLE_SCHEDULER          0   // Task scheduler
#define ENABLE_SYSCALLS           0   // System call interface

// File System Subsystems
#define ENABLE_VFS                0   // Virtual File System
#define ENABLE_IR0FS              0   // IR0 File System
#define ENABLE_EXT2               0   // EXT2 file system support

// Driver Subsystems
#define ENABLE_KEYBOARD_DRIVER    1   // Keyboard input driver
#define ENABLE_ATA_DRIVER         1   // ATA disk driver
#define ENABLE_PS2_DRIVER         1   // PS2 controller driver
#define ENABLE_TIMER_DRIVERS      1   // Timer drivers (PIT, HPET, LAPIC)
#define ENABLE_VGA_DRIVER         1   // VGA display driver

// Debugging and Development
#define ENABLE_DEBUGGING          0   // Debugging system
#define ENABLE_LOGGING            1   // Logging system
#define ENABLE_MEMORY_TESTS       0   // Memory allocation tests
#define ENABLE_STRESS_TESTS       0   // Stress testing

// Shell and User Interface
#define ENABLE_SHELL              0   // Interactive shell
#define ENABLE_GUI                0   // Graphical user interface

// ===============================================================================
// TARGET-SPECIFIC OVERRIDES
// ===============================================================================
// Override subsystem settings based on build target

#ifdef IR0_DESKTOP
    // Desktop: Enable most features
    #undef ENABLE_HEAP_ALLOCATOR
    #undef ENABLE_PROCESS_MANAGEMENT
    #undef ENABLE_SCHEDULER
    #undef ENABLE_VFS
    #undef ENABLE_GUI
    #undef ENABLE_DEBUGGING
    
    #define ENABLE_HEAP_ALLOCATOR     1
    #define ENABLE_PROCESS_MANAGEMENT 1
    #define ENABLE_SCHEDULER          1
    #define ENABLE_VFS                1
    #define ENABLE_GUI                1
    #define ENABLE_DEBUGGING          1

#elif defined(IR0_SERVER)
    // Server: Enable process management, no GUI
    #undef ENABLE_HEAP_ALLOCATOR
    #undef ENABLE_PROCESS_MANAGEMENT
    #undef ENABLE_SCHEDULER
    #undef ENABLE_VFS
    #undef ENABLE_GUI
    
    #define ENABLE_HEAP_ALLOCATOR     1
    #define ENABLE_PROCESS_MANAGEMENT 1
    #define ENABLE_SCHEDULER          1
    #define ENABLE_VFS                1
    #define ENABLE_GUI                0

#elif defined(IR0_IOT)
    // IoT: Minimal features, focus on efficiency
    #undef ENABLE_HEAP_ALLOCATOR
    #undef ENABLE_PROCESS_MANAGEMENT
    #undef ENABLE_SCHEDULER
    #undef ENABLE_VFS
    #undef ENABLE_GUI
    #undef ENABLE_DEBUGGING
    
    #define ENABLE_HEAP_ALLOCATOR     0
    #define ENABLE_PROCESS_MANAGEMENT 0
    #define ENABLE_SCHEDULER          0
    #define ENABLE_VFS                0
    #define ENABLE_GUI                0
    #define ENABLE_DEBUGGING          0

#elif defined(IR0_EMBEDDED)
    // Embedded: Minimal features
    #undef ENABLE_HEAP_ALLOCATOR
    #undef ENABLE_PROCESS_MANAGEMENT
    #undef ENABLE_SCHEDULER
    #undef ENABLE_VFS
    #undef ENABLE_GUI
    #undef ENABLE_DEBUGGING
    #undef ENABLE_LOGGING
    
    #define ENABLE_HEAP_ALLOCATOR     0
    #define ENABLE_PROCESS_MANAGEMENT 0
    #define ENABLE_SCHEDULER          0
    #define ENABLE_VFS                0
    #define ENABLE_GUI                0
    #define ENABLE_DEBUGGING          0
    #define ENABLE_LOGGING            0
#endif

// ===============================================================================
// DEVELOPMENT CONFIGURATIONS
// ===============================================================================
// Special configurations for development and testing

#ifdef IR0_DEVELOPMENT_MODE
    // Development mode: Enable debugging and testing
    #undef ENABLE_DEBUGGING
    #undef ENABLE_MEMORY_TESTS
    #undef ENABLE_STRESS_TESTS
    #undef ENABLE_LOGGING
    
    #define ENABLE_DEBUGGING          1
    #define ENABLE_MEMORY_TESTS       1
    #define ENABLE_STRESS_TESTS       1
    #define ENABLE_LOGGING            1
#endif

#ifdef IR0_TESTING_MODE
    // Testing mode: Focus on testing subsystems
    #undef ENABLE_MEMORY_TESTS
    #undef ENABLE_STRESS_TESTS
    #undef ENABLE_DEBUGGING
    
    #define ENABLE_MEMORY_TESTS       1
    #define ENABLE_STRESS_TESTS       1
    #define ENABLE_DEBUGGING          1
#endif

// ===============================================================================
// DEPENDENCY VALIDATION
// ===============================================================================
// Ensure dependencies are met

#if ENABLE_SCHEDULER && !ENABLE_HEAP_ALLOCATOR
    #error "Scheduler requires heap allocator to be enabled"
#endif

#if ENABLE_PROCESS_MANAGEMENT && !ENABLE_HEAP_ALLOCATOR
    #error "Process management requires heap allocator to be enabled"
#endif

#if ENABLE_VFS && !ENABLE_HEAP_ALLOCATOR
    #error "VFS requires heap allocator to be enabled"
#endif

#if ENABLE_SHELL && !ENABLE_KEYBOARD_DRIVER
    #error "Shell requires keyboard driver to be enabled"
#endif

#if ENABLE_SHELL && !ENABLE_VFS
    #error "Shell requires VFS to be enabled"
#endif

// ===============================================================================
// FEATURE SUMMARY MACROS
// ===============================================================================

#define HAS_MEMORY_MANAGEMENT() (ENABLE_BUMP_ALLOCATOR || ENABLE_HEAP_ALLOCATOR || ENABLE_PHYSICAL_ALLOCATOR)
#define HAS_PROCESS_MANAGEMENT() (ENABLE_PROCESS_MANAGEMENT || ENABLE_SCHEDULER)
#define HAS_FILE_SYSTEM() (ENABLE_VFS || ENABLE_IR0FS)
#define HAS_DRIVERS() (ENABLE_KEYBOARD_DRIVER || ENABLE_ATA_DRIVER || ENABLE_PS2_DRIVER || ENABLE_TIMER_DRIVERS)
#define HAS_DEBUGGING() (ENABLE_DEBUGGING || ENABLE_LOGGING)
#define HAS_USER_INTERFACE() (ENABLE_SHELL || ENABLE_GUI)

// ===============================================================================
// BUILD INFORMATION
// ===============================================================================

// Get build type based on enabled features
#ifdef IR0_DEVELOPMENT_MODE
    #define KERNEL_BUILD_TYPE "DEVELOPMENT"
#elif defined(IR0_TESTING_MODE)
    #define KERNEL_BUILD_TYPE "TESTING"
#elif defined(IR0_DESKTOP)
    #define KERNEL_BUILD_TYPE "DESKTOP"
#elif defined(IR0_SERVER)
    #define KERNEL_BUILD_TYPE "SERVER"
#elif defined(IR0_IOT)
    #define KERNEL_BUILD_TYPE "IoT"
#elif defined(IR0_EMBEDDED)
    #define KERNEL_BUILD_TYPE "EMBEDDED"
#else
    #define KERNEL_BUILD_TYPE "GENERIC"
#endif

// ===============================================================================
// CONFIGURATION FUNCTIONS
// ===============================================================================

// Print subsystem configuration
void subsystem_print_config(void);

// Check if subsystem is enabled
bool subsystem_is_enabled(const char* subsystem_name);

// Get subsystem status
const char* subsystem_get_status(const char* subsystem_name);
