/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Bluetooth HCI Core Layer
 * Copyright (C) 2025  Iván Rodriguez
 *
 * HCI (Host Controller Interface) core functionality
 * Implements basic HCI commands and event handling
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "hci_uart.h"

/* HCI Command Opcodes (OGF | OCF) */
#define HCI_OP_RESET                    0x0C03
#define HCI_OP_READ_BD_ADDR             0x1009
#define HCI_OP_SET_EVENT_MASK           0x0C01
#define HCI_OP_INQUIRY                  0x0401
#define HCI_OP_INQUIRY_CANCEL           0x0402
#define HCI_OP_REMOTE_NAME_REQUEST      0x0419
#define HCI_OP_CREATE_CONN              0x0405
#define HCI_OP_DISCONNECT               0x0406
#define HCI_OP_ACCEPT_CONN_REQ          0x0409

/* HCI Event Codes */
#define HCI_EV_COMMAND_COMPLETE         0x0E
#define HCI_EV_COMMAND_STATUS           0x0F
#define HCI_EV_INQUIRY_RESULT           0x02
#define HCI_EV_CONN_COMPLETE            0x03
#define HCI_EV_DISCONN_COMPLETE         0x05
#define HCI_EV_REMOTE_NAME              0x07

/* HCI Status Codes */
#define HCI_SUCCESS                     0x00
#define HCI_UNKNOWN_COMMAND             0x01
#define HCI_NO_CONNECTION               0x02
#define HCI_HARDWARE_FAILURE            0x03
#define HCI_PAGE_TIMEOUT                0x04

/* Maximum devices we can discover */
#define MAX_BT_DEVICES                  16

/* Bluetooth Device Address length */
#define BD_ADDR_LEN                     6

/* Device name maximum length */
#define BT_NAME_MAX                     248

/* HCI Device structure */
struct hci_device {
    uint8_t bd_addr[BD_ADDR_LEN];    /* Local Bluetooth address */
    bool initialized;                 /* Initialization status */
    struct hci_uart *transport;      /* UART transport layer */
    bool scanning;                    /* Currently scanning */
    uint8_t scan_duration;           /* Scan duration in seconds */
};

/* Discovered Bluetooth device */
struct bluetooth_device {
    uint8_t bd_addr[BD_ADDR_LEN];    /* Device Bluetooth address */
    char name[BT_NAME_MAX];          /* Device name */
    uint8_t device_class[3];         /* Device class */
    int8_t rssi;                     /* Signal strength */
    bool discovered;                 /* Discovery status */
    bool name_resolved;              /* Name resolution status */
};

/**
 * hci_init - Initialize HCI core
 * @uart_base: UART base address for transport
 * 
 * Initializes HCI core layer and underlying transport.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int hci_init(uint16_t uart_base);

/**
 * hci_reset - Send HCI Reset command
 * @hdev: HCI device instance
 * 
 * Resets the Bluetooth controller to default state.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int hci_reset(struct hci_device *hdev);

/**
 * hci_read_bd_addr - Read local Bluetooth address
 * @hdev: HCI device instance
 * @addr: Buffer to store BD address (6 bytes)
 * 
 * Reads the local Bluetooth device address.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int hci_read_bd_addr(struct hci_device *hdev, uint8_t *addr);

/**
 * hci_inquiry - Start device discovery
 * @hdev: HCI device instance
 * @duration: Inquiry duration (1.28s units, max 61.44s)
 * @num_responses: Maximum number of responses (0 = unlimited)
 * 
 * Starts scanning for nearby Bluetooth devices.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int hci_inquiry(struct hci_device *hdev, uint8_t duration, uint8_t num_responses);

/**
 * hci_inquiry_cancel - Cancel ongoing device discovery
 * @hdev: HCI device instance
 * 
 * Cancels ongoing inquiry operation.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int hci_inquiry_cancel(struct hci_device *hdev);

/**
 * hci_remote_name_request - Request remote device name
 * @hdev: HCI device instance
 * @bd_addr: Target device address
 * 
 * Requests the name of a remote Bluetooth device.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int hci_remote_name_request(struct hci_device *hdev, const uint8_t *bd_addr);

/**
 * hci_process_events - Process incoming HCI events
 * @hdev: HCI device instance
 * 
 * Processes any pending HCI events from the controller.
 * Should be called periodically or from interrupt handler.
 * 
 * Returns: Number of events processed, negative on error
 */
int hci_process_events(struct hci_device *hdev);

/**
 * hci_get_device - Get HCI device instance
 * 
 * Returns: Pointer to HCI device instance, NULL if not initialized
 */
struct hci_device *hci_get_device(void);

/**
 * hci_get_discovered_devices - Get array of discovered devices
 * 
 * Returns: Pointer to discovered devices array
 */
struct bluetooth_device *hci_get_discovered_devices(void);

/**
 * hci_get_discovered_count - Get number of discovered devices
 * 
 * Returns: Number of discovered devices
 */
int hci_get_discovered_count(void);

/**
 * hci_clear_discovered_devices - Clear discovered devices list
 */
void hci_clear_discovered_devices(void);