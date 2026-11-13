/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: driver.h
 * Description: IR0 kernel source/header file
 */

// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Driver Interface
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: driver.h
 * Description: Multi-language driver registration and management interface
 */

#ifndef IR0_DRIVER_H
#define IR0_DRIVER_H

#include <stdint.h>
#include <stddef.h>

// Driver language types
typedef enum {
    IR0_DRIVER_LANG_C = 0,
    IR0_DRIVER_LANG_CPP = 1,
    IR0_DRIVER_LANG_RUST = 2,
} ir0_driver_lang_t;

// Driver state
typedef enum {
    IR0_DRIVER_STATE_UNREGISTERED = 0,
    IR0_DRIVER_STATE_REGISTERED = 1,
    IR0_DRIVER_STATE_INITIALIZED = 2,
    IR0_DRIVER_STATE_ACTIVE = 3,
    IR0_DRIVER_STATE_FAILED = 4,
    IR0_DRIVER_STATE_ABSENT = 5, /* probed, no hardware — not a failure */
    IR0_DRIVER_STATE_DEFERRED = 6,
    IR0_DRIVER_STATE_UNSUPPORTED = 7,
} ir0_driver_state_t;

// Return codes
#define IR0_DRIVER_OK           0
#define IR0_DRIVER_ABSENT       1   /* success path: no device present */
#define IR0_DRIVER_ERR          -1
#define IR0_DRIVER_ERR_NOMEM    -2
#define IR0_DRIVER_ERR_INVAL    -3
#define IR0_DRIVER_ERR_EXISTS   -4
#define IR0_DRIVER_ERR_NOTFOUND -5

// Standard driver operations (compatible with C, C++, Rust)
typedef struct ir0_driver_ops {
    // Driver lifecycle
    int32_t (*init)(void);                          // Initialize driver
    int32_t (*probe)(void* device);                 // Probe device
    void    (*remove)(void* device);                // Remove device
    void    (*shutdown)(void);                      // Shutdown driver
    
    // I/O operations
    int32_t (*read)(void* buf, size_t len);         // Read from device
    int32_t (*write)(const void* buf, size_t len);  // Write to device
    int32_t (*ioctl)(uint32_t cmd, void* arg);      // Device control
    
    // Power management
    int32_t (*suspend)(void);                       // Suspend device
    int32_t (*resume)(void);                        // Resume device
} ir0_driver_ops_t;

// Driver metadata
typedef struct ir0_driver_info {
    const char* name;                    // Driver name (must be unique)
    const char* version;                 // Driver version
    const char* author;                  // Driver author
    const char* description;             // Brief description
    ir0_driver_lang_t language;          // Implementation language
} ir0_driver_info_t;

// Driver registration handle (opaque)
typedef struct ir0_driver ir0_driver_t;

// DRIVER REGISTRATION API

/**
 * Register a driver with the kernel
 * 
 * @param info Driver metadata
 * @param ops Driver operations
 * @return Driver handle on success, NULL on failure
 */
ir0_driver_t* ir0_register_driver(const ir0_driver_info_t* info,    const ir0_driver_ops_t* ops);

int ir0_driver_list_to_buffer(char *buf, size_t count);

/**
 * Count registered drivers by outcome for boot summary.
 * ready = INITIALIZED|ACTIVE; the remaining outputs map one-to-one to states.
 */
void ir0_driver_boot_counts(unsigned *ready, unsigned *absent,
			    unsigned *deferred, unsigned *unsupported,
			    unsigned *failed);

/*
 * Emit one KLOG_EVENT_DRIVER_PROBE_RESULT per registered driver
 * (message: "probe name=<n> state=<token>").
 */
void ir0_driver_boot_emit_probe_results(void);

/**
 * Initialize the driver registry subsystem
 * Called during kernel boot
 */
void ir0_driver_registry_init(void);
int ir0_driver_registry_is_initialized(void);

#endif // IR0_DRIVER_H
