// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Driver Initialization
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: drivers/init_drv.c
 * Description: Multi-language driver initialization and registration
 */

#include <ir0/driver.h>
#include <ir0/logging.h>
#include <config.h>
#include <drivers/multilang_drivers.h>
#include "bluetooth/bluetooth_init.h"

/**
 * Initialize multi-language driver subsystem
 * Called during kernel boot after heap is initialized
 */
void drivers_init(void)
{
    /* Initialize driver registry */
    ir0_driver_registry_init();
    log_subsystem_ok("DRIVER_REGISTRY");

    /* Register Bluetooth subsystem */
    if (bluetooth_register_driver() == 0) {
        LOG_INFO("KERNEL", "Bluetooth subsystem registered successfully");
    } else {
        LOG_WARNING("KERNEL", "Bluetooth subsystem registration failed");
    }

    /* Register multi-language example drivers (optional, for testing) */
#if KERNEL_ENABLE_EXAMPLE_DRIVERS
    register_multilang_example_drivers();
    log_subsystem_ok("MULTI_LANG_DRIVERS");
#endif
}

