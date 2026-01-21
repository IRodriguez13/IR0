/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Bluetooth Subsystem Initialization
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Main initialization and registration for Bluetooth subsystem
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * bluetooth_init - Initialize Bluetooth subsystem
 * 
 * Initializes all Bluetooth components:
 * - HCI UART transport
 * - HCI core layer
 * - Device management
 * - Filesystem integration
 * 
 * Returns: 0 on success, negative error code on failure
 */
int bluetooth_init(void);

/**
 * bluetooth_cleanup - Cleanup Bluetooth subsystem
 * 
 * Cleans up all Bluetooth components and frees resources.
 */
void bluetooth_cleanup(void);

/**
 * bluetooth_is_initialized - Check if Bluetooth is initialized
 * 
 * Returns: true if initialized, false otherwise
 */
bool bluetooth_is_initialized(void);

/**
 * bluetooth_get_status - Get Bluetooth subsystem status
 * @buffer: Buffer to store status string
 * @size: Size of buffer
 * 
 * Returns: Number of bytes written to buffer
 */
int bluetooth_get_status(char *buffer, size_t size);

/**
 * bluetooth_register_driver - Register Bluetooth driver with kernel
 * 
 * This function should be called during kernel initialization
 * to register the Bluetooth driver with the driver registry.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int bluetooth_register_driver(void);