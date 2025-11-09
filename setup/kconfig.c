// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: kconfig.c
 * Description: Kernel configuration system for build targets, subsystems, and compilation strategies
 */

#include "kernel_config.h"
#include <ir0/vga.h>
#include <string.h>

const char *ir0_get_target_name(void)
{
    return IR0_TARGET_NAME;
}

const char *ir0_get_target_description(void)
{
    return IR0_TARGET_DESCRIPTION;
}

const char *ir0_get_version_string(void)
{
    return IR0_VERSION_STRING;
}

const char *ir0_get_build_date(void)
{
    return IR0_BUILD_DATE;
}

const char *ir0_get_build_time(void)
{
    return IR0_BUILD_TIME;
}

// ===============================================================================
// SUBSYSTEM CONFIGURATION
// ===============================================================================

typedef struct {
    const char *name;
    bool enabled;
    const char *description;
} subsystem_config_t;

static subsystem_config_t subsystems[] = {
    {"MEMORY", true, "Memory management and allocation"},
    {"PROCESS", true, "Process management and scheduling"},
    {"FILESYSTEM", IR0_ENABLE_FILESYSTEM, "Virtual file system and storage"},
    {"NETWORKING", IR0_ENABLE_NETWORKING, "TCP/IP networking stack"},
    {"AUDIO", IR0_ENABLE_AUDIO, "Audio drivers and sound system"},
    {"GUI", IR0_ENABLE_GUI, "Graphical user interface"},
    {"USB", IR0_ENABLE_USB, "USB device support"},
    {"MULTIMEDIA", IR0_ENABLE_MULTIMEDIA, "Multimedia codecs and playback"},
    {"PRINTING", IR0_ENABLE_PRINTING, "Printer support"},
    {"USER_MODE", IR0_ENABLE_USER_MODE, "User mode process isolation"},
    {"MEMORY_PROTECTION", IR0_ENABLE_MEMORY_PROTECTION, "Memory protection and paging"},
    {"PROCESS_ISOLATION", IR0_ENABLE_PROCESS_ISOLATION, "Process isolation and sandboxing"},
    {NULL, false, NULL}
};

bool ir0_is_subsystem_enabled(const char *subsystem)
{
    for (int i = 0; subsystems[i].name != NULL; i++) {
        if (strcmp(subsystems[i].name, subsystem) == 0) {
            return subsystems[i].enabled;
        }
    }
    return false;
}

void ir0_print_subsystem_status(void)
{
    print("IR0 Kernel Subsystem Configuration:\n");
    print("===================================\n");
    
    for (int i = 0; subsystems[i].name != NULL; i++) {
        print("  ");
        print(subsystems[i].name);
        print(": ");
        if (subsystems[i].enabled) {
            print_success("ENABLED");
        } else {
            print_error("DISABLED");
        }
        print(" - ");
        print(subsystems[i].description);
        print("\n");
    }
    print("\n");
}

// ===============================================================================
// COMPILATION STRATEGY CONFIGURATION
// ===============================================================================

typedef enum {
    COMPILE_STRATEGY_MINIMAL,     // Minimal kernel with basic features
    COMPILE_STRATEGY_STANDARD,    // Standard kernel with common features
    COMPILE_STRATEGY_FULL,        // Full kernel with all features
    COMPILE_STRATEGY_CUSTOM       // Custom configuration
} compile_strategy_t;

typedef struct {
    compile_strategy_t strategy;
    const char *name;
    const char *description;
    bool enable_debug;
    bool enable_optimization;
    bool enable_all_drivers;
    bool enable_advanced_features;
} strategy_config_t;

static strategy_config_t strategies[] = {
    {
        COMPILE_STRATEGY_MINIMAL,
        "Minimal",
        "Minimal kernel for embedded systems",
        false, true, false, false
    },
    {
        COMPILE_STRATEGY_STANDARD,
        "Standard",
        "Standard kernel for desktop/server use",
        true, true, true, false
    },
    {
        COMPILE_STRATEGY_FULL,
        "Full",
        "Full-featured kernel with all capabilities",
        true, false, true, true
    },
    {
        COMPILE_STRATEGY_CUSTOM,
        "Custom",
        "Custom configuration based on build target",
        true, true, true, false
    }
};

compile_strategy_t ir0_get_compile_strategy(void)
{
#ifdef IR0_EMBEDDED
    return COMPILE_STRATEGY_MINIMAL;
#elif defined(IR0_IOT)
    return COMPILE_STRATEGY_MINIMAL;
#elif defined(IR0_SERVER)
    return COMPILE_STRATEGY_FULL;
#elif defined(IR0_DESKTOP)
    return COMPILE_STRATEGY_STANDARD;
#else
    return COMPILE_STRATEGY_CUSTOM;
#endif
}

const char *ir0_get_strategy_name(void)
{
    compile_strategy_t strategy = ir0_get_compile_strategy();
    return strategies[strategy].name;
}

const char *ir0_get_strategy_description(void)
{
    compile_strategy_t strategy = ir0_get_compile_strategy();
    return strategies[strategy].description;
}

bool ir0_strategy_enables_debug(void)
{
    compile_strategy_t strategy = ir0_get_compile_strategy();
    return strategies[strategy].enable_debug;
}

bool ir0_strategy_enables_optimization(void)
{
    compile_strategy_t strategy = ir0_get_compile_strategy();
    return strategies[strategy].enable_optimization;
}

// ===============================================================================
// FEATURE DETECTION AND VALIDATION
// ===============================================================================

bool ir0_is_feature_enabled(const char *feature)
{
    // Audio features
    if (strcmp(feature, "AUDIO") == 0) return IR0_ENABLE_AUDIO;
    if (strcmp(feature, "SOUND_DRIVER") == 0) return IR0_ENABLE_SOUND_DRIVER;
    if (strcmp(feature, "AUDIO_MIXER") == 0) return IR0_ENABLE_AUDIO_MIXER;
    
    // GUI features
    if (strcmp(feature, "GUI") == 0) return IR0_ENABLE_GUI;
    if (strcmp(feature, "VGA_DRIVER") == 0) return IR0_ENABLE_VGA_DRIVER;
    if (strcmp(feature, "FRAMEBUFFER") == 0) return IR0_ENABLE_FRAMEBUFFER;
    if (strcmp(feature, "WINDOW_MANAGER") == 0) return IR0_ENABLE_WINDOW_MANAGER;
    
    // Network features
    if (strcmp(feature, "NETWORKING") == 0) return IR0_ENABLE_NETWORKING;
    if (strcmp(feature, "TCPIP") == 0) return IR0_ENABLE_TCPIP;
    if (strcmp(feature, "SOCKETS") == 0) return IR0_ENABLE_SOCKETS;
    if (strcmp(feature, "ETHERNET") == 0) return IR0_ENABLE_ETHERNET;
    
    // USB features
    if (strcmp(feature, "USB") == 0) return IR0_ENABLE_USB;
    if (strcmp(feature, "USB_DRIVER") == 0) return IR0_ENABLE_USB_DRIVER;
    if (strcmp(feature, "USB_STORAGE") == 0) return IR0_ENABLE_USB_STORAGE;
    if (strcmp(feature, "USB_HID") == 0) return IR0_ENABLE_USB_HID;
    
    // Filesystem features
    if (strcmp(feature, "FILESYSTEM") == 0) return IR0_ENABLE_FILESYSTEM;
    if (strcmp(feature, "VFS") == 0) return IR0_ENABLE_VFS;
    if (strcmp(feature, "EXT2") == 0) return IR0_ENABLE_EXT2;
    if (strcmp(feature, "RAMFS") == 0) return IR0_ENABLE_RAMFS;
    
    // Security features
    if (strcmp(feature, "USER_MODE") == 0) return IR0_ENABLE_USER_MODE;
    if (strcmp(feature, "MEMORY_PROTECTION") == 0) return IR0_ENABLE_MEMORY_PROTECTION;
    if (strcmp(feature, "PROCESS_ISOLATION") == 0) return IR0_ENABLE_PROCESS_ISOLATION;
    
    return false;
}

// ===============================================================================
// BUILD INFORMATION DISPLAY
// ===============================================================================

void ir0_print_build_config(void)
{
    print("IR0 Kernel Build Configuration\n");
    print("==============================\n");
    print("Version: ");
    print(ir0_get_version_string());
    print("\n");
    print("Target: ");
    print(ir0_get_target_name());
    print(" - ");
    print(ir0_get_target_description());
    print("\n");
    print("Strategy: ");
    print(ir0_get_strategy_name());
    print(" - ");
    print(ir0_get_strategy_description());
    print("\n");
    print("Built: ");
    print(ir0_get_build_date());
    print(" ");
    print(ir0_get_build_time());
    print("\n\n");
    
    // System limits
    print("System Limits:\n");
    print("  Max Processes: ");
    print_uint32(IR0_MAX_PROCESSES);
    print("\n");
    print("  Max Threads: ");
    print_uint32(IR0_MAX_THREADS);
    print("\n");
    print("  Heap Size: ");
    print_uint32(IR0_HEAP_SIZE / (1024 * 1024));
    print(" MB\n");
    print("  Scheduler Quantum: ");
    print_uint32(IR0_SCHEDULER_QUANTUM);
    print(" ms\n");
    print("  I/O Buffer Size: ");
    print_uint32(IR0_IO_BUFFER_SIZE / 1024);
    print(" KB\n\n");
    
    // Print subsystem status
    ir0_print_subsystem_status();
}

// ===============================================================================
// CONFIGURATION VALIDATION
// ===============================================================================

bool ir0_validate_config(void)
{
    bool valid = true;
    
    // Check heap size
    if (IR0_HEAP_SIZE < (1024 * 1024)) {
        print_error("ERROR: Heap size too small (minimum 1MB)\n");
        valid = false;
    }
    
    // Check process limits
    if (IR0_MAX_PROCESSES < 1) {
        print_error("ERROR: Invalid max processes configuration\n");
        valid = false;
    }
    
    if (IR0_MAX_THREADS < IR0_MAX_PROCESSES) {
        print_error("ERROR: Max threads must be >= max processes\n");
        valid = false;
    }
    
    // Check scheduler quantum
    if (IR0_SCHEDULER_QUANTUM < 1) {
        print_error("ERROR: Invalid scheduler quantum\n");
        valid = false;
    }
    
    // Check I/O buffer size
    if (IR0_IO_BUFFER_SIZE < 1024) {
        print_error("ERROR: I/O buffer size too small (minimum 1KB)\n");
        valid = false;
    }
    
    // Check feature dependencies
    if (IR0_ENABLE_WINDOW_MANAGER && !IR0_ENABLE_GUI) {
        print_error("ERROR: Window manager requires GUI to be enabled\n");
        valid = false;
    }
    
    if (IR0_ENABLE_SOCKETS && !IR0_ENABLE_NETWORKING) {
        print_error("ERROR: Sockets require networking to be enabled\n");
        valid = false;
    }
    
    if (IR0_ENABLE_USB_STORAGE && !IR0_ENABLE_USB) {
        print_error("ERROR: USB storage requires USB to be enabled\n");
        valid = false;
    }
    
    if (valid) {
        print_success("Configuration validation passed\n");
    } else {
        print_error("Configuration validation failed\n");
    }
    
    return valid;
}

// ===============================================================================
// RUNTIME CONFIGURATION QUERIES
// ===============================================================================

uint32_t ir0_get_max_processes(void)
{
    return IR0_MAX_PROCESSES;
}

uint32_t ir0_get_max_threads(void)
{
    return IR0_MAX_THREADS;
}

uint32_t ir0_get_heap_size(void)
{
    return IR0_HEAP_SIZE;
}

uint32_t ir0_get_scheduler_quantum(void)
{
    return IR0_SCHEDULER_QUANTUM;
}

uint32_t ir0_get_io_buffer_size(void)
{
    return IR0_IO_BUFFER_SIZE;
}