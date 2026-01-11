// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Multi-Language Driver Registration Implementation
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: drivers/multilang_drivers.c
 * Description: Implementation of multi-language driver registration
 */

#include <drivers/multilang_drivers.h>
#include <ir0/driver.h>
#include <ir0/logging.h>

/* Forward declarations for driver registration functions */
/* These are defined in the Rust/C++ driver source files */

/* Rust drivers */
extern void* register_rust_simple_driver(void);

/* C++ drivers */
extern ir0_driver_t* register_cpp_example_driver(void);

/**
 * Register all multi-language example drivers
 * 
 * @return Number of drivers successfully registered
 */
int register_multilang_example_drivers(void)
{
    int registered = 0;
    ir0_driver_t* driver;

    /* Register Rust simple driver (minimal test driver) */
    driver = (ir0_driver_t*)register_rust_simple_driver();
    if (driver) {
        registered++;
        LOG_INFO("MultiLangDrivers", "Registered rust_simple driver");
    } else {
        LOG_WARNING("MultiLangDrivers", "Failed to register rust_simple driver (may not be compiled)");
    }

    /* Register C++ example driver */
    driver = register_cpp_example_driver();
    if (driver) {
        registered++;
        LOG_INFO("MultiLangDrivers", "Registered cpp_example driver");
    } else {
        LOG_WARNING("MultiLangDrivers", "Failed to register cpp_example driver (may not be compiled)");
    }

    if (registered > 0) {
        LOG_INFO_FMT("MultiLangDrivers", "Successfully registered %d multi-language driver(s)", registered);
    } else {
        LOG_WARNING("MultiLangDrivers", "No multi-language drivers were registered (they may not be compiled)");
    }

    return registered;
}

