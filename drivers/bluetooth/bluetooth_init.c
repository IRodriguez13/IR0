/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: bluetooth_init.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Bluetooth Subsystem Initialization
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Main initialization and driver registration for Bluetooth
 */

#include "bluetooth_init.h"
#include "bt_device.h"
#include "hci_core.h"
#include "hci_uart.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <ir0/bluetooth.h>
#include <ir0/driver.h>
#include <ir0/idle.h>
#include <ir0/ktm/klog.h>

static void bluetooth_cleanup(void);

/* Bluetooth subsystem state */
static bool bluetooth_initialized = false;

/* Driver information for registration */
static ir0_driver_info_t bluetooth_driver_info = {
    .name = "bluetooth",
    .version = "1.0.0",
    .author = "Iván Rodriguez",
    .description = "Bluetooth HCI subsystem with device discovery",
    .language = IR0_DRIVER_LANG_C
};

/* Driver operations */
static int32_t bluetooth_driver_init(void);
static void bluetooth_driver_shutdown(void);

static ir0_driver_ops_t bluetooth_driver_ops = {
    .init = bluetooth_driver_init,
    .probe = NULL,
    .remove = NULL,
    .shutdown = bluetooth_driver_shutdown,
    .read = NULL,
    .write = NULL,
    .ioctl = NULL,
    .suspend = NULL,
    .resume = NULL
};

/**
 * bluetooth_driver_init - Driver initialization callback
 */
static int32_t bluetooth_driver_init(void)
{
    klog_info("BLUETOOTH", "Initializing Bluetooth driver");
    int ret = bluetooth_init();
    return ret == 0 ? IR0_DRIVER_OK : IR0_DRIVER_ERR;
}

/**
 * bluetooth_driver_shutdown - Driver shutdown callback
 */
static void bluetooth_driver_shutdown(void)
{
    klog_info("BLUETOOTH", "Shutting down Bluetooth driver");
    bluetooth_cleanup();
}

/**
 * bluetooth_init - Initialize Bluetooth subsystem
 */
int bluetooth_init(void)
{
    if (bluetooth_initialized) {
        klog_info("BLUETOOTH", "Already initialized");
        return 0;
    }
    
    klog_info("BLUETOOTH", "Starting Bluetooth subsystem initialization");
    
    /* Initialize device management (this will init HCI core and UART) */
    int ret = bt_device_init();
    if (ret < 0) {
        klog_error("BLUETOOTH", "Failed to initialize device management");
        return ret;
    }
    
    /* Skip HCI reset during boot to avoid blocking
     * The clock system may not be fully initialized yet, and we don't want
     * to wait for hardware that may not exist (like in QEMU).
     * HCI reset will happen on first open of /dev/bluetooth/hci0
     */
    klog_info("BLUETOOTH", "HCI reset deferred until first device open");
    
    bluetooth_initialized = true;
    /*
     * The HCI UART has no RX IRQ. The stub linked when Bluetooth is absent
     * is a different file and does not register.
     */
    kernel_idle_poll_register(ir0_bluetooth_poll);

    klog_info("BLUETOOTH", "Bluetooth subsystem initialized successfully");
    klog_info("BLUETOOTH", "Available interfaces:");
    klog_info("BLUETOOTH", "  /dev/bluetooth/hci0 - HCI control interface");
    klog_info("BLUETOOTH", "  /proc/bluetooth/devices - Discovered devices");
    klog_info("BLUETOOTH", "  /proc/bluetooth/scan - Scan control");
    
    return 0;
}

/**
 * bluetooth_cleanup - Cleanup Bluetooth subsystem
 */
static void bluetooth_cleanup(void)
{
    if (!bluetooth_initialized) {
        return;
    }
    
    klog_info("BLUETOOTH", "Cleaning up Bluetooth subsystem");
    
    /* Cleanup device management */
    bt_device_cleanup();
    
    bluetooth_initialized = false;
    
    klog_info("BLUETOOTH", "Bluetooth subsystem cleaned up");
}

/**
 * bluetooth_poll - Process pending HCI events
 *
 * Llamado desde el bucle principal para drenar la UART y procesar Inquiry Result
 * durante el scan; si no se llama, los dispositivos descubiertos no aparecen.
 */
void bluetooth_poll(void)
{
    if (!bluetooth_initialized)
        return;
    hci_process_events();
    bt_scan_state_sync();
}

/**
 * bluetooth_register_driver - Register Bluetooth driver with kernel
 * 
 * This function should be called during kernel initialization
 * to register the Bluetooth driver with the driver registry.
 */
int bluetooth_register_driver(void)
{
    klog_info("BLUETOOTH", "Registering Bluetooth driver");
    
    ir0_driver_t *driver = ir0_register_driver(&bluetooth_driver_info, &bluetooth_driver_ops);
    if (!driver) {
        klog_error("BLUETOOTH", "Failed to register Bluetooth driver");
        return -1;
    }
    
    klog_info("BLUETOOTH", "Bluetooth driver registered successfully");
    return 0;
}