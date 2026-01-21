/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Bluetooth Device Management Implementation
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Implements device management and filesystem integration
 */

#include "bt_device.h"
#include "hci_core.h"
#include "hci_uart.h"
#include <stdint.h>
#include <ir0/kmem.h>
#include <string.h>
#include <errno.h>
#include <ir0/logging.h>

/* Global device manager instance */
static struct bt_device_manager *bt_manager = NULL;

/* Event buffer for HCI events (circular buffer) */
#define EVENT_BUFFER_SIZE 1024
static uint8_t event_buffer[EVENT_BUFFER_SIZE];
static size_t event_buffer_head = 0;
static size_t event_buffer_tail = 0;
static size_t event_buffer_count = 0;

/* Helper function to add event to buffer - TODO: Use for real-time event processing */
/*
static void add_event_to_buffer(const uint8_t *event, size_t len)
{
    if (len > EVENT_BUFFER_SIZE - 4) 
    {
        return; // Event too large
    }
    
    // Add length prefix
    event_buffer[event_buffer_head] = (len >> 8) & 0xFF;
    event_buffer_head = (event_buffer_head + 1) % EVENT_BUFFER_SIZE;
    event_buffer[event_buffer_head] = len & 0xFF;
    event_buffer_head = (event_buffer_head + 1) % EVENT_BUFFER_SIZE;
    
    // Add event data
    for (size_t i = 0; i < len; i++) {
        event_buffer[event_buffer_head] = event[i];
        event_buffer_head = (event_buffer_head + 1) % EVENT_BUFFER_SIZE;
    }
    
    event_buffer_count += len + 2;
    
    // Prevent buffer overflow by removing old events
    while (event_buffer_count > EVENT_BUFFER_SIZE - 256) {
        // Remove oldest event
        uint16_t old_len = (event_buffer[event_buffer_tail] << 8) | 
                          event_buffer[(event_buffer_tail + 1) % EVENT_BUFFER_SIZE];
        event_buffer_tail = (event_buffer_tail + old_len + 2) % EVENT_BUFFER_SIZE;
        event_buffer_count -= old_len + 2;
    }
}
*/

/* Helper function to get event from buffer */
static int get_event_from_buffer(uint8_t *event, size_t max_len)
{
    if (event_buffer_count < 2) {
        return 0; /* No events available */
    }
    
    /* Read length prefix */
    uint16_t len = (event_buffer[event_buffer_tail] << 8) | 
                   event_buffer[(event_buffer_tail + 1) % EVENT_BUFFER_SIZE];
    
    if (len > max_len) {
        return -EINVAL; /* Buffer too small */
    }
    
    /* Skip length prefix */
    event_buffer_tail = (event_buffer_tail + 2) % EVENT_BUFFER_SIZE;
    
    /* Read event data */
    for (size_t i = 0; i < len; i++) {
        event[i] = event_buffer[event_buffer_tail];
        event_buffer_tail = (event_buffer_tail + 1) % EVENT_BUFFER_SIZE;
    }
    
    event_buffer_count -= len + 2;
    return (int)len;
}

/**
 * bt_device_init - Initialize Bluetooth device management
 */
int bt_device_init(void)
{
    if (bt_manager) {
        return 0; /* Already initialized */
    }
    
    /* Allocate device manager */
    bt_manager = (struct bt_device_manager *)kmalloc(sizeof(struct bt_device_manager));
    if (!bt_manager) {
        LOG_ERROR("BT_DEV", "Failed to allocate device manager");
        return -ENOMEM;
    }
    
    memset(bt_manager, 0, sizeof(struct bt_device_manager));
    
    /* Initialize HCI core (COM1 = 0x3F8) */
    int ret = hci_init(0x3F8);
    if (ret < 0) {
        LOG_ERROR("BT_DEV", "Failed to initialize HCI core");
        kfree(bt_manager);
        bt_manager = NULL;
        return ret;
    }
    
    bt_manager->hci_dev = hci_get_device();
    bt_manager->initialized = true;
    bt_manager->scan_active = 0;
    
    /* Initialize event buffer */
    event_buffer_head = 0;
    event_buffer_tail = 0;
    event_buffer_count = 0;
    
    LOG_INFO("BT_DEV", "Bluetooth device management initialized");
    return 0;
}

/**
 * bt_device_cleanup - Cleanup Bluetooth device management
 */
void bt_device_cleanup(void)
{
    if (bt_manager) {
        /* Stop any ongoing scan */
        if (bt_manager->scan_active) {
            hci_inquiry_cancel(bt_manager->hci_dev);
        }
        
        kfree(bt_manager);
        bt_manager = NULL;
        
        LOG_INFO("BT_DEV", "Bluetooth device management cleaned up");
    }
}

/**
 * bt_device_get_manager - Get device manager instance
 */
struct bt_device_manager *bt_device_get_manager(void)
{
    return bt_manager;
}

/**
 * bt_hci_open - Open /dev/bluetooth/hci0 device
 */
int bt_hci_open(void)
{
    if (!bt_manager || !bt_manager->initialized) {
        return -ENODEV;
    }
    
    LOG_DEBUG("BT_DEV", "HCI device opened");
    return 0;
}

/**
 * bt_hci_close - Close /dev/bluetooth/hci0 device
 */
int bt_hci_close(void)
{
    if (!bt_manager || !bt_manager->initialized) {
        return -ENODEV;
    }
    
    LOG_DEBUG("BT_DEV", "HCI device closed");
    return 0;
}

/**
 * bt_hci_read - Read from /dev/bluetooth/hci0 device
 */
int bt_hci_read(char *buffer, size_t count)
{
    if (!bt_manager || !bt_manager->initialized) {
        return -ENODEV;
    }
    
    if (!buffer || count == 0) {
        return -EINVAL;
    }
    
    /* Process any pending HCI events first */
    hci_process_events(bt_manager->hci_dev);
    
    /* Try to get event from buffer */
    uint8_t event[256];
    int len = get_event_from_buffer(event, sizeof(event));
    
    if (len <= 0) {
        return len; /* No events or error */
    }
    
    if ((size_t)len > count) {
        len = (int)count; /* Truncate if buffer too small */
    }
    
    memcpy(buffer, event, len);
    return len;
}

/**
 * bt_hci_write - Write to /dev/bluetooth/hci0 device
 */
int bt_hci_write(const char *buffer, size_t count)
{
    if (!bt_manager || !bt_manager->initialized) {
        return -ENODEV;
    }
    
    if (!buffer || count == 0) {
        return -EINVAL;
    }
    
    /* Send raw HCI command */
    int ret = hci_uart_send_command((const uint8_t *)buffer, count);
    if (ret < 0) {
        LOG_ERROR("BT_DEV", "Failed to send HCI command");
        return ret;
    }
    
    LOG_DEBUG_FMT("BT_DEV", "HCI command sent (%zu bytes)", count);
    return (int)count;
}

/**
 * bt_hci_ioctl - IOCTL for /dev/bluetooth/hci0 device
 */
int bt_hci_ioctl(unsigned int cmd, unsigned long arg)
{
    if (!bt_manager || !bt_manager->initialized) {
        return -ENODEV;
    }
    
    switch (cmd) {
        case BT_IOCTL_RESET:
            LOG_INFO("BT_DEV", "IOCTL: Reset requested");
            return hci_reset(bt_manager->hci_dev);
            
        case BT_IOCTL_READ_BD_ADDR:
            LOG_INFO("BT_DEV", "IOCTL: Read BD_ADDR requested");
            if (arg) {
                return hci_read_bd_addr(bt_manager->hci_dev, (uint8_t *)arg);
            }
            return -EINVAL;
            
        case BT_IOCTL_START_INQUIRY:
            {
                struct bt_inquiry_params *params = (struct bt_inquiry_params *)arg;
                if (!params) {
                    /* Default parameters */
                    LOG_INFO("BT_DEV", "IOCTL: Start inquiry (default params)");
                    bt_manager->scan_active = 1;
                    return hci_inquiry(bt_manager->hci_dev, 10, 0); /* 10 * 1.28s = 12.8s */
                } else {
                    LOG_INFO_FMT("BT_DEV", "IOCTL: Start inquiry (duration: %d, max: %d)",
                               params->duration, params->max_responses);
                    bt_manager->scan_active = 1;
                    return hci_inquiry(bt_manager->hci_dev, params->duration, params->max_responses);
                }
            }
            
        case BT_IOCTL_STOP_INQUIRY:
            LOG_INFO("BT_DEV", "IOCTL: Stop inquiry requested");
            bt_manager->scan_active = 0;
            return hci_inquiry_cancel(bt_manager->hci_dev);
            
        case BT_IOCTL_CLEAR_DEVICES:
            LOG_INFO("BT_DEV", "IOCTL: Clear devices requested");
            hci_clear_discovered_devices();
            return 0;
            
        case BT_IOCTL_GET_DEVICE_COUNT:
            return hci_get_discovered_count();
            
        default:
            LOG_WARNING_FMT("BT_DEV", "Unknown IOCTL command: 0x%X", cmd);
            return -ENOTTY;
    }
}

/**
 * bt_proc_devices_read - Read /proc/bluetooth/devices
 */
int bt_proc_devices_read(char *buffer, size_t count)
{
    if (!bt_manager || !bt_manager->initialized) {
        return -ENODEV;
    }
    
    if (!buffer || count == 0) {
        return -EINVAL;
    }
    
    struct bluetooth_device *devices = hci_get_discovered_devices();
    int device_count = hci_get_discovered_count();
    size_t offset = 0;
    
    /* Header */
    int written = snprintf(buffer + offset, count - offset,
                          "Discovered Bluetooth Devices (%d):\n", device_count);
    if (written < 0 || (size_t)written >= count - offset) {
        return (int)offset;
    }
    offset += written;
    
    if (device_count == 0) {
        written = snprintf(buffer + offset, count - offset,
                          "No devices discovered. Use 'echo start > /proc/bluetooth/scan' to scan.\n");
        if (written > 0 && (size_t)written < count - offset) {
            offset += written;
        }
        return (int)offset;
    }
    
    /* List devices */
    for (int i = 0; i < device_count && offset < count - 100; i++) {
        struct bluetooth_device *dev = &devices[i];
        
        if (!dev->discovered) {
            continue;
        }
        
        written = snprintf(buffer + offset, count - offset,
                          "%02X:%02X:%02X:%02X:%02X:%02X %s (Class: %02X:%02X:%02X, RSSI: %d)\n",
                          dev->bd_addr[5], dev->bd_addr[4], dev->bd_addr[3],
                          dev->bd_addr[2], dev->bd_addr[1], dev->bd_addr[0],
                          dev->name_resolved ? dev->name : "Unknown",
                          dev->device_class[2], dev->device_class[1], dev->device_class[0],
                          dev->rssi);
        
        if (written > 0 && (size_t)written < count - offset) {
            offset += written;
        } else {
            break;
        }
    }
    
    return (int)offset;
}

/**
 * bt_proc_scan_read - Read /proc/bluetooth/scan
 */
int bt_proc_scan_read(char *buffer, size_t count)
{
    if (!bt_manager || !bt_manager->initialized) {
        return -ENODEV;
    }
    
    if (!buffer || count == 0) {
        return -EINVAL;
    }
    
    size_t offset = 0;
    int written;
    
    /* Scan status */
    if (bt_manager->scan_active) {
        written = snprintf(buffer + offset, count - offset,
                          "Status: Scanning for devices...\n");
    } else {
        written = snprintf(buffer + offset, count - offset,
                          "Status: Not scanning\n");
    }
    
    if (written > 0 && (size_t)written < count - offset) {
        offset += written;
    }
    
    /* Instructions */
    written = snprintf(buffer + offset, count - offset,
                      "\nCommands:\n"
                      "  echo start > /proc/bluetooth/scan  - Start scanning\n"
                      "  echo stop > /proc/bluetooth/scan   - Stop scanning\n"
                      "  echo clear > /proc/bluetooth/scan  - Clear device list\n"
                      "\nDiscovered devices: %d\n",
                      hci_get_discovered_count());
    
    if (written > 0 && (size_t)written < count - offset) {
        offset += written;
    }
    
    return (int)offset;
}

/**
 * bt_proc_scan_write - Write to /proc/bluetooth/scan (command processing)
 */
int bt_proc_scan_write(const char *command)
{
    if (!bt_manager || !bt_manager->initialized) {
        return -ENODEV;
    }
    
    if (!command) {
        return -EINVAL;
    }
    
    LOG_INFO_FMT("BT_DEV", "Received scan command: '%s'", command);
    
    if (strcmp(command, "start") == 0) {
        /* Start scanning */
        if (bt_manager->scan_active) {
            LOG_WARNING("BT_DEV", "Scan already active");
            return -EBUSY;
        }
        
        bt_manager->scan_active = 1;
        int ret = hci_inquiry(bt_manager->hci_dev, 10, 0); /* 10 * 1.28s = 12.8s, unlimited responses */
        if (ret < 0) {
            bt_manager->scan_active = 0;
            LOG_ERROR("BT_DEV", "Failed to start inquiry");
            return ret;
        }
        
        LOG_INFO("BT_DEV", "Bluetooth scan started");
        return 0;
        
    } else if (strcmp(command, "stop") == 0) {
        /* Stop scanning */
        if (!bt_manager->scan_active) {
            LOG_WARNING("BT_DEV", "No scan active");
            return 0; /* Not an error */
        }
        
        bt_manager->scan_active = 0;
        int ret = hci_inquiry_cancel(bt_manager->hci_dev);
        if (ret < 0) {
            LOG_ERROR("BT_DEV", "Failed to cancel inquiry");
            return ret;
        }
        
        LOG_INFO("BT_DEV", "Bluetooth scan stopped");
        return 0;
        
    } else if (strcmp(command, "clear") == 0) {
        /* Clear discovered devices */
        hci_clear_discovered_devices();
        LOG_INFO("BT_DEV", "Discovered devices cleared");
        return 0;
        
    } else {
        LOG_WARNING_FMT("BT_DEV", "Unknown scan command: '%s'", command);
        return -EINVAL;
    }
}