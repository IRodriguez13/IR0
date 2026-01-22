/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Bluetooth HCI Core
 * Copyright (C) 2025  Iván Rodriguez
 *
 * HCI Core layer for Bluetooth device management
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* HCI Opcodes */
#define HCI_OPCODE_RESET                   0x0c03
#define HCI_OPCODE_READ_BD_ADDR            0x1009
#define HCI_OPCODE_SET_EVENT_MASK          0x0c01
#define HCI_OPCODE_INQUIRY                 0x0401
#define HCI_OPCODE_INQUIRY_CANCEL          0x0402
#define HCI_OPCODE_REMOTE_NAME_REQUEST     0x0419

/* HCI Event Codes */
#define HCI_EVENT_COMMAND_COMPLETE          0x0e
#define HCI_EVENT_COMMAND_STATUS            0x0f
#define HCI_EVENT_INQUIRY_RESULT            0x02
#define HCI_EVENT_INQUIRY_COMPLETE          0x01
#define HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE 0x07

/* HCI Status Codes */
#define HCI_STATUS_SUCCESS                 0x00

/* Bluetooth Device Address (6 bytes) */
typedef uint8_t bd_addr_t[6];

/* Bluetooth Device Structure */
struct bluetooth_device {
    bd_addr_t bd_addr;      /* Bluetooth Device Address */
    char name[248];         /* Device name (max 248 chars) */
    uint8_t device_class[3]; /* Device class */
    int8_t rssi;            /* Signal strength */
    bool discovered;        /* Discovery status */
    uint32_t last_seen;     /* Timestamp */
};

/* HCI Device Structure */
struct hci_device {
    bd_addr_t bd_addr;      /* Local BD_ADDR */
    bool initialized;       /* Initialization status */
    bool scanning;          /* Inquiry scan in progress */
};

/**
 * hci_core_init - Initialize HCI core
 * 
 * Returns: 0 on success, negative error code on failure
 */
int hci_core_init(void);

/**
 * hci_reset - Send HCI Reset command
 * 
 * Resets the Bluetooth controller.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int hci_reset(void);

/**
 * hci_read_bd_addr - Read Bluetooth Device Address
 * @addr: Buffer to store BD_ADDR (6 bytes)
 * 
 * Reads the local Bluetooth device address.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int hci_read_bd_addr(bd_addr_t addr);

/**
 * hci_set_event_mask - Set HCI event mask
 * @mask: 8-byte event mask
 * 
 * Sets which events the controller should report.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int hci_set_event_mask(const uint8_t *mask);

/**
 * hci_inquiry - Start device discovery
 * @duration: Inquiry duration in units of 1.28s (1-61)
 * @num_responses: Maximum number of responses (0 = unlimited)
 * 
 * Starts Bluetooth device discovery/inquiry.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int hci_inquiry(uint8_t duration, uint8_t num_responses);

/**
 * hci_inquiry_cancel - Cancel device discovery
 * 
 * Cancels ongoing inquiry scan.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int hci_inquiry_cancel(void);

/**
 * hci_remote_name_request - Request remote device name
 * @bd_addr: Bluetooth address of remote device
 * 
 * Requests the friendly name of a remote device.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int hci_remote_name_request(const bd_addr_t bd_addr);

/**
 * hci_get_discovered_devices - Get list of discovered devices
 * @devices: Buffer to store device list
 * @max_devices: Maximum number of devices to return
 * 
 * Returns: Number of discovered devices, negative error on failure
 */
int hci_get_discovered_devices(struct bluetooth_device *devices, int max_devices);

/**
 * hci_get_device - Get HCI device instance
 * 
 * Returns: Pointer to HCI device, NULL if not initialized
 */
struct hci_device *hci_get_device(void);

/**
 * hci_process_events - Process pending HCI events
 * 
 * Should be called periodically to process incoming events.
 * 
 * Returns: Number of events processed
 */
int hci_process_events(void);

/**
 * hci_clear_discovered_devices - Clear discovered devices list
 * 
 * Removes all discovered devices from the internal list.
 * 
 * Returns: 0 on success, negative error on failure
 */
int hci_clear_discovered_devices(void);
