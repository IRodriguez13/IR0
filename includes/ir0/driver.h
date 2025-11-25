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
} ir0_driver_state_t;

// Return codes
#define IR0_DRIVER_OK           0
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
ir0_driver_t* ir0_register_driver(const ir0_driver_info_t* info, 
                                   const ir0_driver_ops_t* ops);

/**
 * Unregister a driver from the kernel
 * 
 * @param driver Driver handle
 * @return 0 on success, negative on error
 */
int32_t ir0_unregister_driver(ir0_driver_t* driver);

/**
 * Find a driver by name
 * 
 * @param name Driver name
 * @return Driver handle or NULL if not found
 */
ir0_driver_t* ir0_find_driver(const char* name);

/**
 * Get driver state
 * 
 * @param driver Driver handle
 * @return Driver state
 */
ir0_driver_state_t ir0_driver_get_state(ir0_driver_t* driver);

/**
 * Initialize the driver registry subsystem
 * Called during kernel boot
 */
void ir0_driver_registry_init(void);

// SIMPLIFIED REGISTRATION API (for simple drivers)

/**
 * Register a simple driver (C compatibility)
 * 
 * @param name Driver name (must be unique)
 * @param ops Driver operations
 * @return 0 on success, negative on error
 */
static inline int32_t ir0_register_simple_driver(const char* name, 
                                                  const ir0_driver_ops_t* ops)
{
    ir0_driver_info_t info = {
        .name = name,
        .version = "1.0",
        .author = "Unknown",
        .description = "IR0 Driver",
        .language = IR0_DRIVER_LANG_C
    };
    
    ir0_driver_t* drv = ir0_register_driver(&info, ops);
    return drv ? IR0_DRIVER_OK : IR0_DRIVER_ERR;
}

// HELPER MACROS

/**
 * Define a driver with automatic registration
 * Usage:
 *   IR0_DEFINE_DRIVER(my_driver, "My Driver", &my_ops);
 */
#define IR0_DEFINE_DRIVER(var_name, drv_name, drv_ops) \
    static ir0_driver_t* var_name = NULL; \
    static void __attribute__((constructor)) var_name##_init(void) { \
        ir0_driver_info_t info = { \
            .name = drv_name, \
            .version = "1.0", \
            .author = "IR0 Team", \
            .description = drv_name, \
            .language = IR0_DRIVER_LANG_C \
        }; \
        var_name = ir0_register_driver(&info, drv_ops); \
    }

/**
 * Driver initialization macro (for easy init function creation)
 */
#define IR0_DRIVER_INIT(func_name) \
    int32_t func_name(void)

/**
 * Driver probe macro
 */
#define IR0_DRIVER_PROBE(func_name) \
    int32_t func_name(void* device)

#endif // IR0_DRIVER_H
