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
#include <ir0/logging.h>
#include <ir0/driver.h>

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
    LOG_INFO("BLUETOOTH", "Initializing Bluetooth driver");
    int ret = bluetooth_init();
    return ret == 0 ? IR0_DRIVER_OK : IR0_DRIVER_ERR;
}

/**
 * bluetooth_driver_shutdown - Driver shutdown callback
 */
static void bluetooth_driver_shutdown(void)
{
    LOG_INFO("BLUETOOTH", "Shutting down Bluetooth driver");
    bluetooth_cleanup();
}

/**
 * bluetooth_init - Initialize Bluetooth subsystem
 */
int bluetooth_init(void)
{
    if (bluetooth_initialized) {
        LOG_INFO("BLUETOOTH", "Already initialized");
        return 0;
    }
    
    LOG_INFO("BLUETOOTH", "Starting Bluetooth subsystem initialization");
    
    /* Initialize device management (this will init HCI core and UART) */
    int ret = bt_device_init();
    if (ret < 0) {
        LOG_ERROR("BLUETOOTH", "Failed to initialize device management");
        return ret;
    }
    
    /* Reset the HCI controller */
    struct hci_device *hci_dev = hci_get_device();
    if (hci_dev) {
        ret = hci_reset(hci_dev);
        if (ret < 0) {
            LOG_WARNING("BLUETOOTH", "HCI reset failed, continuing anyway");
        } else {
            LOG_INFO("BLUETOOTH", "HCI controller reset successful");
        }
    }
    
    bluetooth_initialized = true;
    
    LOG_INFO("BLUETOOTH", "Bluetooth subsystem initialized successfully");
    LOG_INFO("BLUETOOTH", "Available interfaces:");
    LOG_INFO("BLUETOOTH", "  /dev/bluetooth/hci0 - HCI control interface");
    LOG_INFO("BLUETOOTH", "  /proc/bluetooth/devices - Discovered devices");
    LOG_INFO("BLUETOOTH", "  /proc/bluetooth/scan - Scan control");
    
    return 0;
}

/**
 * bluetooth_cleanup - Cleanup Bluetooth subsystem
 */
void bluetooth_cleanup(void)
{
    if (!bluetooth_initialized) {
        return;
    }
    
    LOG_INFO("BLUETOOTH", "Cleaning up Bluetooth subsystem");
    
    /* Cleanup device management */
    bt_device_cleanup();
    
    bluetooth_initialized = false;
    
    LOG_INFO("BLUETOOTH", "Bluetooth subsystem cleaned up");
}

/**
 * bluetooth_is_initialized - Check if Bluetooth is initialized
 */
bool bluetooth_is_initialized(void)
{
    return bluetooth_initialized;
}

/**
 * bluetooth_get_status - Get Bluetooth subsystem status
 */
int bluetooth_get_status(char *buffer, size_t size)
{
    if (!buffer || size == 0) {
        return -EINVAL;
    }
    
    size_t offset = 0;
    int written;
    
    /* Basic status */
    written = snprintf(buffer + offset, size - offset,
                      "Bluetooth Subsystem Status:\n"
                      "  Initialized: %s\n",
                      bluetooth_initialized ? "Yes" : "No");
    
    if (written > 0 && (size_t)written < size - offset) {
        offset += written;
    }
    
    if (!bluetooth_initialized) {
        return (int)offset;
    }
    
    /* HCI device status */
    struct hci_device *hci_dev = hci_get_device();
    if (hci_dev) {
        written = snprintf(buffer + offset, size - offset,
                          "  HCI Device: Initialized\n"
                          "  Transport: UART (COM1)\n"
                          "  Scanning: %s\n",
                          hci_dev->scanning ? "Active" : "Inactive");
        
        if (written > 0 && (size_t)written < size - offset) {
            offset += written;
        }
    }
    
    /* Device discovery status */
    int device_count = hci_get_discovered_count();
    written = snprintf(buffer + offset, size - offset,
                      "  Discovered Devices: %d\n",
                      device_count);
    
    if (written > 0 && (size_t)written < size - offset) {
        offset += written;
    }
    
    /* Available interfaces */
    written = snprintf(buffer + offset, size - offset,
                      "\nAvailable Interfaces:\n"
                      "  /dev/bluetooth/hci0 - HCI control\n"
                      "  /proc/bluetooth/devices - Device list\n"
                      "  /proc/bluetooth/scan - Scan control\n");
    
    if (written > 0 && (size_t)written < size - offset) {
        offset += written;
    }
    
    return (int)offset;
}

/**
 * bluetooth_register_driver - Register Bluetooth driver with kernel
 * 
 * This function should be called during kernel initialization
 * to register the Bluetooth driver with the driver registry.
 */
int bluetooth_register_driver(void)
{
    LOG_INFO("BLUETOOTH", "Registering Bluetooth driver");
    
    ir0_driver_t *driver = ir0_register_driver(&bluetooth_driver_info, &bluetooth_driver_ops);
    if (!driver) {
        LOG_ERROR("BLUETOOTH", "Failed to register Bluetooth driver");
        return -1;
    }
    
    LOG_INFO("BLUETOOTH", "Bluetooth driver registered successfully");
    return 0;
}