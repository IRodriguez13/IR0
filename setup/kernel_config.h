// setup/kernel_config.h - IR0 Kernel Configuration System
#pragma once

#include <stdint.h>
#include <stdbool.h>

// ===============================================================================
// KERNEL VERSION AND BUILD INFO
// ===============================================================================

#define IR0_VERSION_MAJOR 1
#define IR0_VERSION_MINOR 0
#define IR0_VERSION_PATCH 0
#define IR0_VERSION_STRING "1.0.0"

// Build information
#define IR0_BUILD_DATE __DATE__
#define IR0_BUILD_TIME __TIME__

// ===============================================================================
// BUILD TARGET CONFIGURATIONS
// ===============================================================================

// Target-specific feature flags
#ifdef IR0_DESKTOP
    #define IR0_TARGET_NAME "Desktop"
    #define IR0_TARGET_DESCRIPTION "Desktop/Workstation kernel with GUI support"
    
    // Desktop-specific features
    #define IR0_ENABLE_GUI 1
    #define IR0_ENABLE_AUDIO 1
    #define IR0_ENABLE_USB 1
    #define IR0_ENABLE_NETWORKING 1
    #define IR0_ENABLE_FILESYSTEM 1
    #define IR0_ENABLE_MULTIMEDIA 1
    #define IR0_ENABLE_PRINTING 1
    
    // Memory configuration
    #define IR0_DESKTOP_HEAP_SIZE (256 * 1024 * 1024)  // 256MB
    #define IR0_DESKTOP_MAX_PROCESSES 1024
    #define IR0_DESKTOP_MAX_THREADS 4096
    
    // Performance settings
    #define IR0_DESKTOP_SCHEDULER_QUANTUM 10  // ms
    #define IR0_DESKTOP_IO_BUFFER_SIZE (64 * 1024)  // 64KB
    
    // Security features
    #define IR0_ENABLE_USER_MODE 1
    #define IR0_ENABLE_MEMORY_PROTECTION 1
    #define IR0_ENABLE_PROCESS_ISOLATION 1

#elif defined(IR0_SERVER)
    #define IR0_TARGET_NAME "Server"
    #define IR0_TARGET_DESCRIPTION "High-performance server kernel"
    
    // Server-specific features
    #define IR0_ENABLE_GUI 0
    #define IR0_ENABLE_AUDIO 0
    #define IR0_ENABLE_USB 1
    #define IR0_ENABLE_NETWORKING 1
    #define IR0_ENABLE_FILESYSTEM 1
    #define IR0_ENABLE_MULTIMEDIA 0
    #define IR0_ENABLE_PRINTING 0
    
    // Memory configuration
    #define IR0_SERVER_HEAP_SIZE (1024 * 1024 * 1024)  // 1GB
    #define IR0_SERVER_MAX_PROCESSES 4096
    #define IR0_SERVER_MAX_THREADS 16384
    
    // Performance settings
    #define IR0_SERVER_SCHEDULER_QUANTUM 5   // ms
    #define IR0_SERVER_IO_BUFFER_SIZE (256 * 1024)  // 256KB
    
    // Security features
    #define IR0_ENABLE_USER_MODE 1
    #define IR0_ENABLE_MEMORY_PROTECTION 1
    #define IR0_ENABLE_PROCESS_ISOLATION 1
    #define IR0_ENABLE_NETWORK_SECURITY 1

#elif defined(IR0_IOT)
    #define IR0_TARGET_NAME "IoT"
    #define IR0_TARGET_DESCRIPTION "Lightweight IoT/Embedded kernel"
    
    // IoT-specific features
    #define IR0_ENABLE_GUI 0
    #define IR0_ENABLE_AUDIO 0
    #define IR0_ENABLE_USB 0
    #define IR0_ENABLE_NETWORKING 1
    #define IR0_ENABLE_FILESYSTEM 1
    #define IR0_ENABLE_MULTIMEDIA 0
    #define IR0_ENABLE_PRINTING 0
    
    // Memory configuration
    #define IR0_IOT_HEAP_SIZE (16 * 1024 * 1024)  // 16MB
    #define IR0_IOT_MAX_PROCESSES 64
    #define IR0_IOT_MAX_THREADS 256
    
    // Performance settings
    #define IR0_IOT_SCHEDULER_QUANTUM 20  // ms
    #define IR0_IOT_IO_BUFFER_SIZE (4 * 1024)  // 4KB
    
    // Power management
    #define IR0_ENABLE_POWER_MANAGEMENT 1
    #define IR0_ENABLE_SLEEP_MODES 1
    #define IR0_ENABLE_LOW_POWER_TIMERS 1
    
    // Security features
    #define IR0_ENABLE_USER_MODE 0
    #define IR0_ENABLE_MEMORY_PROTECTION 1
    #define IR0_ENABLE_PROCESS_ISOLATION 0

#elif defined(IR0_EMBEDDED)
    #define IR0_TARGET_NAME "Embedded"
    #define IR0_TARGET_DESCRIPTION "Minimal embedded kernel"
    
    // Embedded-specific features
    #define IR0_ENABLE_GUI 0
    #define IR0_ENABLE_AUDIO 0
    #define IR0_ENABLE_USB 0
    #define IR0_ENABLE_NETWORKING 0
    #define IR0_ENABLE_FILESYSTEM 0
    #define IR0_ENABLE_MULTIMEDIA 0
    #define IR0_ENABLE_PRINTING 0
    
    // Memory configuration
    #define IR0_EMBEDDED_HEAP_SIZE (4 * 1024 * 1024)  // 4MB
    #define IR0_EMBEDDED_MAX_PROCESSES 16
    #define IR0_EMBEDDED_MAX_THREADS 64
    
    // Performance settings
    #define IR0_EMBEDDED_SCHEDULER_QUANTUM 50  // ms
    #define IR0_EMBEDDED_IO_BUFFER_SIZE (1 * 1024)  // 1KB
    
    // Power management
    #define IR0_ENABLE_POWER_MANAGEMENT 1
    #define IR0_ENABLE_SLEEP_MODES 1
    #define IR0_ENABLE_LOW_POWER_TIMERS 1
    
    // Security features
    #define IR0_ENABLE_USER_MODE 0
    #define IR0_ENABLE_MEMORY_PROTECTION 0
    #define IR0_ENABLE_PROCESS_ISOLATION 0

#else
    // Default configuration (Desktop-like)
    #define IR0_TARGET_NAME "Generic"
    #define IR0_TARGET_DESCRIPTION "Generic kernel configuration"
    
    #define IR0_ENABLE_GUI 0
    #define IR0_ENABLE_AUDIO 0
    #define IR0_ENABLE_USB 0
    #define IR0_ENABLE_NETWORKING 0
    #define IR0_ENABLE_FILESYSTEM 1
    #define IR0_ENABLE_MULTIMEDIA 0
    #define IR0_ENABLE_PRINTING 0
    
    #define IR0_GENERIC_HEAP_SIZE (64 * 1024 * 1024)  // 64MB
    #define IR0_GENERIC_MAX_PROCESSES 256
    #define IR0_GENERIC_MAX_THREADS 1024
    
    #define IR0_GENERIC_SCHEDULER_QUANTUM 15  // ms
    #define IR0_GENERIC_IO_BUFFER_SIZE (16 * 1024)  // 16KB
    
    #define IR0_ENABLE_USER_MODE 0
    #define IR0_ENABLE_MEMORY_PROTECTION 1
    #define IR0_ENABLE_PROCESS_ISOLATION 0
#endif

// ===============================================================================
// FEATURE CONFIGURATION MACROS
// ===============================================================================

// Memory management
#if IR0_ENABLE_FILESYSTEM
    #define IR0_ENABLE_VFS 1
    #define IR0_ENABLE_EXT2 1
    #define IR0_ENABLE_RAMFS 1
#else
    #define IR0_ENABLE_VFS 0
    #define IR0_ENABLE_EXT2 0
    #define IR0_ENABLE_RAMFS 0
#endif

// Networking
#if IR0_ENABLE_NETWORKING
    #define IR0_ENABLE_TCPIP 1
    #define IR0_ENABLE_SOCKETS 1
    #define IR0_ENABLE_ETHERNET 1
#else
    #define IR0_ENABLE_TCPIP 0
    #define IR0_ENABLE_SOCKETS 0
    #define IR0_ENABLE_ETHERNET 0
#endif

// Device drivers
#if IR0_ENABLE_USB
    #define IR0_ENABLE_USB_DRIVER 1
    #define IR0_ENABLE_USB_STORAGE 1
    #define IR0_ENABLE_USB_HID 1
#else
    #define IR0_ENABLE_USB_DRIVER 0
    #define IR0_ENABLE_USB_STORAGE 0
    #define IR0_ENABLE_USB_HID 0
#endif

// GUI system
#if IR0_ENABLE_GUI
    #define IR0_ENABLE_VGA_DRIVER 1
    #define IR0_ENABLE_FRAMEBUFFER 1
    #define IR0_ENABLE_WINDOW_MANAGER 1
#else
    #define IR0_ENABLE_VGA_DRIVER 1  // Always enable basic VGA
    #define IR0_ENABLE_FRAMEBUFFER 0
    #define IR0_ENABLE_WINDOW_MANAGER 0
#endif

// Audio system
#if IR0_ENABLE_AUDIO
    #define IR0_ENABLE_SOUND_DRIVER 1
    #define IR0_ENABLE_AUDIO_MIXER 1
#else
    #define IR0_ENABLE_SOUND_DRIVER 0
    #define IR0_ENABLE_AUDIO_MIXER 0
#endif

// ===============================================================================
// SYSTEM LIMITS
// ===============================================================================

// Get target-specific limits
#ifdef IR0_DESKTOP
    #define IR0_HEAP_SIZE IR0_DESKTOP_HEAP_SIZE
    #define IR0_MAX_PROCESSES IR0_DESKTOP_MAX_PROCESSES
    #define IR0_MAX_THREADS IR0_DESKTOP_MAX_THREADS
    #define IR0_SCHEDULER_QUANTUM IR0_DESKTOP_SCHEDULER_QUANTUM
    #define IR0_IO_BUFFER_SIZE IR0_DESKTOP_IO_BUFFER_SIZE
#elif defined(IR0_SERVER)
    #define IR0_HEAP_SIZE IR0_SERVER_HEAP_SIZE
    #define IR0_MAX_PROCESSES IR0_SERVER_MAX_PROCESSES
    #define IR0_MAX_THREADS IR0_SERVER_MAX_THREADS
    #define IR0_SCHEDULER_QUANTUM IR0_SERVER_SCHEDULER_QUANTUM
    #define IR0_IO_BUFFER_SIZE IR0_SERVER_IO_BUFFER_SIZE
#elif defined(IR0_IOT)
    #define IR0_HEAP_SIZE IR0_IOT_HEAP_SIZE
    #define IR0_MAX_PROCESSES IR0_IOT_MAX_PROCESSES
    #define IR0_MAX_THREADS IR0_IOT_MAX_THREADS
    #define IR0_SCHEDULER_QUANTUM IR0_IOT_SCHEDULER_QUANTUM
    #define IR0_IO_BUFFER_SIZE IR0_IOT_IO_BUFFER_SIZE
#elif defined(IR0_EMBEDDED)
    #define IR0_HEAP_SIZE IR0_EMBEDDED_HEAP_SIZE
    #define IR0_MAX_PROCESSES IR0_EMBEDDED_MAX_PROCESSES
    #define IR0_MAX_THREADS IR0_EMBEDDED_MAX_THREADS
    #define IR0_SCHEDULER_QUANTUM IR0_EMBEDDED_SCHEDULER_QUANTUM
    #define IR0_IO_BUFFER_SIZE IR0_EMBEDDED_IO_BUFFER_SIZE
#else
    #define IR0_HEAP_SIZE IR0_GENERIC_HEAP_SIZE
    #define IR0_MAX_PROCESSES IR0_GENERIC_MAX_PROCESSES
    #define IR0_MAX_THREADS IR0_GENERIC_MAX_THREADS
    #define IR0_SCHEDULER_QUANTUM IR0_GENERIC_SCHEDULER_QUANTUM
    #define IR0_IO_BUFFER_SIZE IR0_GENERIC_IO_BUFFER_SIZE
#endif

// ===============================================================================
// CONFIGURATION FUNCTIONS
// ===============================================================================

// Get build configuration info
const char* ir0_get_target_name(void);
const char* ir0_get_target_description(void);
const char* ir0_get_version_string(void);
const char* ir0_get_build_date(void);
const char* ir0_get_build_time(void);

// Check feature availability
bool ir0_is_feature_enabled(const char* feature);
void ir0_print_build_config(void);

// ===============================================================================
// CONFIGURATION VALIDATION
// ===============================================================================

// Validate configuration at compile time
#if IR0_HEAP_SIZE < (1024 * 1024)
    #error "Heap size too small for any target"
#endif

#if IR0_MAX_PROCESSES < 1
    #error "Invalid max processes configuration"
#endif

#if IR0_MAX_THREADS < IR0_MAX_PROCESSES
    #error "Max threads must be >= max processes"
#endif

#if IR0_SCHEDULER_QUANTUM < 1
    #error "Invalid scheduler quantum"
#endif

#if IR0_IO_BUFFER_SIZE < 1024
    #error "IO buffer size too small"
#endif
