// setup/kernel_config.c - IR0 Kernel Configuration Implementation
#include "kernel_config.h"
#include <ir0/print.h>
#include <string.h>

// ===============================================================================
// CONFIGURATION FUNCTIONS IMPLEMENTATION
// ===============================================================================

const char* ir0_get_target_name(void) {
    return IR0_TARGET_NAME;
}

const char* ir0_get_target_description(void) {
    return IR0_TARGET_DESCRIPTION;
}

const char* ir0_get_version_string(void) {
    return IR0_VERSION_STRING;
}

const char* ir0_get_build_date(void) {
    return IR0_BUILD_DATE;
}

const char* ir0_get_build_time(void) {
    return IR0_BUILD_TIME;
}

bool ir0_is_feature_enabled(const char* feature) {
    if (!feature) return false;
    
    if (strcmp(feature, "GUI") == 0) {
        return IR0_ENABLE_GUI;
    } else if (strcmp(feature, "AUDIO") == 0) {
        return IR0_ENABLE_AUDIO;
    } else if (strcmp(feature, "USB") == 0) {
        return IR0_ENABLE_USB;
    } else if (strcmp(feature, "NETWORKING") == 0) {
        return IR0_ENABLE_NETWORKING;
    } else if (strcmp(feature, "FILESYSTEM") == 0) {
        return IR0_ENABLE_FILESYSTEM;
    } else if (strcmp(feature, "MULTIMEDIA") == 0) {
        return IR0_ENABLE_MULTIMEDIA;
    } else if (strcmp(feature, "PRINTING") == 0) {
        return IR0_ENABLE_PRINTING;
    } else if (strcmp(feature, "VFS") == 0) {
        return IR0_ENABLE_VFS;
    } else if (strcmp(feature, "TCPIP") == 0) {
        return IR0_ENABLE_TCPIP;
    } else if (strcmp(feature, "SOCKETS") == 0) {
        return IR0_ENABLE_SOCKETS;
    } else if (strcmp(feature, "ETHERNET") == 0) {
        return IR0_ENABLE_ETHERNET;
    } else if (strcmp(feature, "USB_DRIVER") == 0) {
        return IR0_ENABLE_USB_DRIVER;
    } else if (strcmp(feature, "VGA_DRIVER") == 0) {
        return IR0_ENABLE_VGA_DRIVER;
    } else if (strcmp(feature, "FRAMEBUFFER") == 0) {
        return IR0_ENABLE_FRAMEBUFFER;
    } else if (strcmp(feature, "WINDOW_MANAGER") == 0) {
        return IR0_ENABLE_WINDOW_MANAGER;
    } else if (strcmp(feature, "SOUND_DRIVER") == 0) {
        return IR0_ENABLE_SOUND_DRIVER;
    } else if (strcmp(feature, "AUDIO_MIXER") == 0) {
        return IR0_ENABLE_AUDIO_MIXER;
    } else if (strcmp(feature, "USER_MODE") == 0) {
        return IR0_ENABLE_USER_MODE;
    } else if (strcmp(feature, "MEMORY_PROTECTION") == 0) {
        return IR0_ENABLE_MEMORY_PROTECTION;
    } else if (strcmp(feature, "PROCESS_ISOLATION") == 0) {
        return IR0_ENABLE_PROCESS_ISOLATION;
    } else if (strcmp(feature, "POWER_MANAGEMENT") == 0) {
        #ifdef IR0_ENABLE_POWER_MANAGEMENT
            return IR0_ENABLE_POWER_MANAGEMENT;
        #else
            return false;
        #endif
    } else if (strcmp(feature, "SLEEP_MODES") == 0) {
        #ifdef IR0_ENABLE_SLEEP_MODES
            return IR0_ENABLE_SLEEP_MODES;
        #else
            return false;
        #endif
    } else if (strcmp(feature, "LOW_POWER_TIMERS") == 0) {
        #ifdef IR0_ENABLE_LOW_POWER_TIMERS
            return IR0_ENABLE_LOW_POWER_TIMERS;
        #else
            return false;
        #endif
    } else if (strcmp(feature, "NETWORK_SECURITY") == 0) {
        #ifdef IR0_ENABLE_NETWORK_SECURITY
            return IR0_ENABLE_NETWORK_SECURITY;
        #else
            return false;
        #endif
    }
    
    return false;
}

void ir0_print_build_config(void) {
    print_colored("=== IR0 KERNEL BUILD CONFIGURATION ===\n", 0x0A, 0x00);
    
    print_colored("Target: ", 0x0B, 0x00);
    print(ir0_get_target_name());
    print(" - ");
    print(ir0_get_target_description());
    print("\n");
    
    print_colored("Version: ", 0x0B, 0x00);
    print(ir0_get_version_string());
    print("\n");
    
    print_colored("Build Date: ", 0x0B, 0x00);
    print(ir0_get_build_date());
    print(" at ");
    print(ir0_get_build_time());
    print("\n");
    
    print_colored("System Limits:\n", 0x0B, 0x00);
    print_colored("  Heap Size: ", 0x0E, 0x00);
    print_hex_compact(IR0_HEAP_SIZE / (1024 * 1024));
    print(" MB\n");
    
    print_colored("  Max Processes: ", 0x0E, 0x00);
    print_hex_compact(IR0_MAX_PROCESSES);
    print("\n");
    
    print_colored("  Max Threads: ", 0x0E, 0x00);
    print_hex_compact(IR0_MAX_THREADS);
    print("\n");
    
    print_colored("  Scheduler Quantum: ", 0x0E, 0x00);
    print_hex_compact(IR0_SCHEDULER_QUANTUM);
    print(" ms\n");
    
    print_colored("  IO Buffer Size: ", 0x0E, 0x00);
    print_hex_compact(IR0_IO_BUFFER_SIZE / 1024);
    print(" KB\n");
    
    print_colored("Enabled Features:\n", 0x0B, 0x00);
    
    const char* features[] = {
        "GUI", "AUDIO", "USB", "NETWORKING", "FILESYSTEM", 
        "MULTIMEDIA", "PRINTING", "VFS", "TCPIP", "SOCKETS",
        "ETHERNET", "USB_DRIVER", "VGA_DRIVER", "FRAMEBUFFER",
        "WINDOW_MANAGER", "SOUND_DRIVER", "AUDIO_MIXER",
        "USER_MODE", "MEMORY_PROTECTION", "PROCESS_ISOLATION",
        "POWER_MANAGEMENT", "SLEEP_MODES", "LOW_POWER_TIMERS",
        "NETWORK_SECURITY"
    };
    
    int enabled_count = 0;
    for (size_t i = 0; i < sizeof(features) / sizeof(features[0]); i++) 
    {
        if (ir0_is_feature_enabled(features[i])) {
            print_colored("  ✅ ", 0x0A, 0x00);
            print(features[i]);
            print("\n");
            enabled_count++;
        }
    }
    
    print_colored("Feature Summary: ", 0x0B, 0x00);
    print_hex_compact(enabled_count);
    print(" features enabled out of ");
    print_hex_compact(sizeof(features) / sizeof(features[0]));
    print(" total\n");
    
    print_colored("==========================================\n", 0x0A, 0x00);
}
