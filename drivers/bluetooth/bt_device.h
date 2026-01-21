/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Bluetooth Device Management
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Device management and filesystem integration for Bluetooth
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Bluetooth device management */
struct bt_device_manager {
    bool initialized;
    struct hci_device *hci_dev;
    int scan_active;
    uint32_t scan_start_time;
};

/* IOCTL commands for /dev/bluetooth/hci0 */
#define BT_IOCTL_RESET              0x1000
#define BT_IOCTL_READ_BD_ADDR       0x1001
#define BT_IOCTL_START_INQUIRY      0x1002
#define BT_IOCTL_STOP_INQUIRY       0x1003
#define BT_IOCTL_CLEAR_DEVICES      0x1004
#define BT_IOCTL_GET_DEVICE_COUNT   0x1005

/* IOCTL parameter structures */
struct bt_inquiry_params {
    uint8_t duration;        /* Inquiry duration (1.28s units) */
    uint8_t max_responses;   /* Maximum responses (0 = unlimited) */
};

struct bt_device_info {
    uint8_t bd_addr[6];      /* Bluetooth address */
    char name[248];          /* Device name */
    uint8_t device_class[3]; /* Device class */
    int8_t rssi;            /* Signal strength */
    bool name_resolved;      /* Name resolution status */
};

/**
 * bt_device_init - Initialize Bluetooth device management
 * 
 * Returns: 0 on success, negative error code on failure
 */
int bt_device_init(void);

/**
 * bt_device_cleanup - Cleanup Bluetooth device management
 */
void bt_device_cleanup(void);

/**
 * bt_device_get_manager - Get device manager instance
 * 
 * Returns: Pointer to device manager, NULL if not initialized
 */
struct bt_device_manager *bt_device_get_manager(void);

/* Device file operations for /dev/bluetooth/hci0 */
int bt_hci_open(void);
int bt_hci_close(void);
int bt_hci_read(char *buffer, size_t count);
int bt_hci_write(const char *buffer, size_t count);
int bt_hci_ioctl(unsigned int cmd, unsigned long arg);

/* Proc filesystem operations */
int bt_proc_devices_read(char *buffer, size_t count);
int bt_proc_scan_read(char *buffer, size_t count);
int bt_proc_scan_write(const char *command);