/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Bluetooth HCI Core Layer Implementation
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Implements HCI commands, event processing, and device management
 */

#include "hci_core.h"
#include "hci_uart.h"
#include <stdint.h>
#include <ir0/kmem.h>
#include <string.h>
#include <errno.h>
#include <ir0/logging.h>

/* Global HCI device instance */
static struct hci_device *hci_dev = NULL;

/* Discovered devices array */
static struct bluetooth_device discovered_devices[MAX_BT_DEVICES];
static int discovered_count = 0;

/* Helper function to build HCI command packet */
static int build_hci_command(uint8_t *buffer, uint16_t opcode, 
                            const uint8_t *params, uint8_t param_len)
{
    if (!buffer) {
        return -EINVAL;
    }
    
    buffer[0] = opcode & 0xFF;        /* Opcode LSB */
    buffer[1] = (opcode >> 8) & 0xFF; /* Opcode MSB */
    buffer[2] = param_len;            /* Parameter length */
    
    if (params && param_len > 0) {
        memcpy(&buffer[3], params, param_len);
    }
    
    return 3 + param_len; /* Total command length */
}

/**
 * hci_init - Initialize HCI core
 */
int hci_init(uint16_t uart_base)
{
    if (hci_dev) {
        /* Already initialized */
        return 0;
    }
    
    /* Allocate HCI device structure */
    hci_dev = (struct hci_device *)kmalloc(sizeof(struct hci_device));
    if (!hci_dev) {
        LOG_ERROR("HCI", "Failed to allocate HCI device structure");
        return -ENOMEM;
    }
    
    memset(hci_dev, 0, sizeof(struct hci_device));
    
    /* Initialize UART transport */
    int ret = hci_uart_init(uart_base);
    if (ret < 0) {
        LOG_ERROR("HCI", "Failed to initialize UART transport");
        kfree(hci_dev);
        hci_dev = NULL;
        return ret;
    }
    
    hci_dev->transport = hci_uart_get_instance();
    hci_dev->initialized = true;
    hci_dev->scanning = false;
    
    /* Clear discovered devices */
    memset(discovered_devices, 0, sizeof(discovered_devices));
    discovered_count = 0;
    
    LOG_INFO("HCI", "HCI core initialized successfully");
    return 0;
}

/**
 * hci_reset - Send HCI Reset command
 */
int hci_reset(struct hci_device *hdev)
{
    if (!hdev || !hdev->initialized) {
        return -ENODEV;
    }
    
    uint8_t cmd[3];
    int len = build_hci_command(cmd, HCI_OP_RESET, NULL, 0);
    
    int ret = hci_uart_send_command(cmd, len);
    if (ret < 0) {
        LOG_ERROR("HCI", "Failed to send Reset command");
        return ret;
    }
    
    LOG_INFO("HCI", "Reset command sent");
    return 0;
}

/**
 * hci_read_bd_addr - Read local Bluetooth address
 */
int hci_read_bd_addr(struct hci_device *hdev, uint8_t *addr)
{
    if (!hdev || !hdev->initialized || !addr) {
        return -EINVAL;
    }
    
    uint8_t cmd[3];
    int len = build_hci_command(cmd, HCI_OP_READ_BD_ADDR, NULL, 0);
    
    int ret = hci_uart_send_command(cmd, len);
    if (ret < 0) {
        LOG_ERROR("HCI", "Failed to send Read BD_ADDR command");
        return ret;
    }
    
    LOG_INFO("HCI", "Read BD_ADDR command sent");
    return 0;
}

/**
 * hci_inquiry - Start device discovery
 */
int hci_inquiry(struct hci_device *hdev, uint8_t duration, uint8_t num_responses)
{
    if (!hdev || !hdev->initialized) {
        return -ENODEV;
    }
    
    if (hdev->scanning) {
        LOG_WARNING("HCI", "Inquiry already in progress");
        return -EBUSY;
    }
    
    /* Inquiry parameters: LAP (3 bytes), duration (1 byte), num_responses (1 byte) */
    uint8_t params[5] = {
        0x33, 0x8B, 0x9E,  /* General Inquiry LAP */
        duration,          /* Inquiry duration */
        num_responses      /* Max responses */
    };
    
    uint8_t cmd[8];
    int len = build_hci_command(cmd, HCI_OP_INQUIRY, params, sizeof(params));
    
    int ret = hci_uart_send_command(cmd, len);
    if (ret < 0) {
        LOG_ERROR("HCI", "Failed to send Inquiry command");
        return ret;
    }
    
    hdev->scanning = true;
    hdev->scan_duration = duration;
    
    LOG_INFO_FMT("HCI", "Inquiry started (duration: %d, max_responses: %d)", 
             duration, num_responses);
    return 0;
}

/**
 * hci_inquiry_cancel - Cancel ongoing device discovery
 */
int hci_inquiry_cancel(struct hci_device *hdev)
{
    if (!hdev || !hdev->initialized) {
        return -ENODEV;
    }
    
    if (!hdev->scanning) {
        LOG_WARNING("HCI", "No inquiry in progress");
        return 0;
    }
    
    uint8_t cmd[3];
    int len = build_hci_command(cmd, HCI_OP_INQUIRY_CANCEL, NULL, 0);
    
    int ret = hci_uart_send_command(cmd, len);
    if (ret < 0) {
        LOG_ERROR("HCI", "Failed to send Inquiry Cancel command");
        return ret;
    }
    
    hdev->scanning = false;
    
    LOG_INFO("HCI", "Inquiry cancelled");
    return 0;
}

/**
 * hci_remote_name_request - Request remote device name
 */
int hci_remote_name_request(struct hci_device *hdev, const uint8_t *bd_addr)
{
    if (!hdev || !hdev->initialized || !bd_addr) {
        return -EINVAL;
    }
    
    /* Remote Name Request parameters: BD_ADDR (6 bytes) + other params */
    uint8_t params[10];
    memcpy(params, bd_addr, 6);      /* BD_ADDR */
    params[6] = 0x01;                /* Page Scan Repetition Mode */
    params[7] = 0x00;                /* Reserved */
    params[8] = 0x00;                /* Clock Offset LSB */
    params[9] = 0x00;                /* Clock Offset MSB */
    
    uint8_t cmd[13];
    int len = build_hci_command(cmd, HCI_OP_REMOTE_NAME_REQUEST, params, sizeof(params));
    
    int ret = hci_uart_send_command(cmd, len);
    if (ret < 0) {
        LOG_ERROR("HCI", "Failed to send Remote Name Request command");
        return ret;
    }
    
    LOG_INFO_FMT("HCI", "Remote Name Request sent for %02X:%02X:%02X:%02X:%02X:%02X",
             bd_addr[5], bd_addr[4], bd_addr[3], bd_addr[2], bd_addr[1], bd_addr[0]);
    return 0;
}

/* Helper function to process Inquiry Result event */
static void process_inquiry_result(const uint8_t *event_data, uint8_t event_len)
{
    if (event_len < 15) { /* Minimum size for one inquiry result */
        LOG_WARNING("HCI", "Inquiry Result event too short");
        return;
    }
    
    uint8_t num_responses = event_data[0];
    const uint8_t *data = &event_data[1];
    
    for (int i = 0; i < num_responses && discovered_count < MAX_BT_DEVICES; i++) {
        if (data + 14 > event_data + event_len) {
            break; /* Prevent buffer overflow */
        }
        
        struct bluetooth_device *dev = &discovered_devices[discovered_count];
        
        /* Copy BD_ADDR (6 bytes) */
        memcpy(dev->bd_addr, data, 6);
        data += 6;
        
        /* Skip Page Scan Repetition Mode (1 byte) */
        data += 1;
        
        /* Skip Reserved (2 bytes) */
        data += 2;
        
        /* Copy Device Class (3 bytes) */
        memcpy(dev->device_class, data, 3);
        data += 3;
        
        /* Skip Clock Offset (2 bytes) */
        data += 2;
        
        /* Set default values */
        dev->discovered = true;
        dev->name_resolved = false;
        dev->rssi = -50; /* Default RSSI (not available in basic inquiry) */
        strcpy(dev->name, "Unknown");
        
        discovered_count++;
        
        LOG_INFO_FMT("HCI", "Device discovered: %02X:%02X:%02X:%02X:%02X:%02X (Class: %02X:%02X:%02X)",
                 dev->bd_addr[5], dev->bd_addr[4], dev->bd_addr[3],
                 dev->bd_addr[2], dev->bd_addr[1], dev->bd_addr[0],
                 dev->device_class[2], dev->device_class[1], dev->device_class[0]);
    }
}

/* Helper function to process Remote Name Request Complete event */
static void process_remote_name_complete(const uint8_t *event_data, uint8_t event_len)
{
    if (event_len < 7) { /* Status + BD_ADDR minimum */
        LOG_WARNING("HCI", "Remote Name Complete event too short");
        return;
    }
    
    uint8_t status = event_data[0];
    const uint8_t *bd_addr = &event_data[1];
    
    if (status != HCI_SUCCESS) {
        LOG_WARNING_FMT("HCI", "Remote Name Request failed (status: 0x%02X)", status);
        return;
    }
    
    /* Find device in discovered list */
    for (int i = 0; i < discovered_count; i++) {
        if (memcmp(discovered_devices[i].bd_addr, bd_addr, 6) == 0) {
            /* Copy name (up to 248 bytes, null-terminated) */
            int name_len = event_len - 7; /* Remaining bytes after status + BD_ADDR */
            if (name_len > BT_NAME_MAX - 1) {
                name_len = BT_NAME_MAX - 1;
            }
            
            memcpy(discovered_devices[i].name, &event_data[7], name_len);
            discovered_devices[i].name[name_len] = '\0';
            discovered_devices[i].name_resolved = true;
            
            LOG_INFO_FMT("HCI", "Device name resolved: %02X:%02X:%02X:%02X:%02X:%02X = \"%s\"",
                     bd_addr[5], bd_addr[4], bd_addr[3], bd_addr[2], bd_addr[1], bd_addr[0],
                     discovered_devices[i].name);
            break;
        }
    }
}

/**
 * hci_process_events - Process incoming HCI events
 */
int hci_process_events(struct hci_device *hdev)
{
    if (!hdev || !hdev->initialized) {
        return -ENODEV;
    }
    
    uint8_t event[256];
    int events_processed = 0;
    
    /* Process all available events */
    while (hci_uart_is_data_available()) {
        int len = hci_uart_receive_event(event, sizeof(event));
        if (len <= 0) {
            break;
        }
        
        if (len < 2) {
            LOG_WARNING_FMT("HCI", "Event too short (len: %d)", len);
            continue;
        }
        
        uint8_t event_code = event[0];
        uint8_t param_len = event[1];
        
        LOG_DEBUG_FMT("HCI", "Processing event 0x%02X (len: %d)", event_code, param_len);
        
        switch (event_code) {
            case HCI_EV_COMMAND_COMPLETE:
                LOG_DEBUG("HCI", "Command Complete event received");
                break;
                
            case HCI_EV_COMMAND_STATUS:
                LOG_DEBUG("HCI", "Command Status event received");
                break;
                
            case HCI_EV_INQUIRY_RESULT:
                LOG_INFO("HCI", "Inquiry Result event received");
                if (len >= 3) {
                    process_inquiry_result(&event[2], param_len);
                }
                break;
                
            case HCI_EV_REMOTE_NAME:
                LOG_INFO("HCI", "Remote Name Complete event received");
                if (len >= 3) {
                    process_remote_name_complete(&event[2], param_len);
                }
                break;
                
            default:
                LOG_DEBUG_FMT("HCI", "Unhandled event: 0x%02X", event_code);
                break;
        }
        
        events_processed++;
    }
    
    return events_processed;
}

/**
 * hci_get_device - Get HCI device instance
 */
struct hci_device *hci_get_device(void)
{
    return hci_dev;
}

/**
 * hci_get_discovered_devices - Get array of discovered devices
 */
struct bluetooth_device *hci_get_discovered_devices(void)
{
    return discovered_devices;
}

/**
 * hci_get_discovered_count - Get number of discovered devices
 */
int hci_get_discovered_count(void)
{
    return discovered_count;
}

/**
 * hci_clear_discovered_devices - Clear discovered devices list
 */
void hci_clear_discovered_devices(void)
{
    memset(discovered_devices, 0, sizeof(discovered_devices));
    discovered_count = 0;
    LOG_INFO("HCI", "Discovered devices list cleared");
}