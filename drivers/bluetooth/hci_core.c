/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Bluetooth HCI Core Implementation
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Implements HCI command/event handling for Bluetooth
 */

#include "hci_core.h"
#include "hci_uart.h"
#include <ir0/kmem.h>
#include <string.h>
#include <ir0/errno.h>
#include <errno.h>
#include <drivers/timer/clock_system.h>
#include <stdint.h>

/* Helper: Get uptime in milliseconds */
static uint32_t hci_get_uptime_ms(void)
{
    return (uint32_t)clock_get_uptime_milliseconds();
}

/* Maximum number of discovered devices */
#define MAX_BT_DEVICES 32

/* HCI device instance */
static struct hci_device *hci_dev = NULL;

/* Discovered devices list */
static struct bluetooth_device discovered_devices[MAX_BT_DEVICES];
static int num_discovered_devices = 0;

/**
 * hci_build_command_packet - Build HCI command packet
 * @opcode: HCI opcode (16-bit)
 * @params: Command parameters
 * @param_len: Length of parameters
 * @buf: Output buffer
 * @buf_len: Buffer size
 * 
 * Returns: Packet length on success, negative error on failure
 */
static int hci_build_command_packet(uint16_t opcode, const uint8_t *params, 
                                     uint8_t param_len, uint8_t *buf, size_t buf_len)
{
    if (!buf || buf_len < 3)
        return -EINVAL;
    
    buf[0] = (uint8_t)(opcode & 0xFF);        /* Opcode LSB */
    buf[1] = (uint8_t)((opcode >> 8) & 0xFF); /* Opcode MSB */
    buf[2] = param_len;                       /* Parameter length */
    
    if (param_len > 0 && params)
    {
        if (buf_len < (size_t)(3 + param_len))
            return -EINVAL;
        
        memcpy(&buf[3], params, param_len);
    }
    
    return 3 + param_len;
}

/**
 * hci_parse_event - Parse HCI event packet
 * @event: Event packet (without packet type byte)
 * @event_len: Event packet length
 * @event_code: Output event code
 * @params: Output parameter buffer
 * @param_len: Output parameter length
 * 
 * Returns: 0 on success, negative error on failure
 */
static int hci_parse_event(const uint8_t *event, size_t event_len,
                           uint8_t *event_code, uint8_t *params, uint8_t *param_len)
{
    if (!event || event_len < 2 || !event_code || !params || !param_len)
        return -EINVAL;
    
    *event_code = event[0];
    *param_len = event[1];
    
    if (event_len < (size_t)(2 + *param_len))
        return -EINVAL;
    
    if (*param_len > 0)
        memcpy(params, &event[2], *param_len);
    
    return 0;
}

/**
 * hci_wait_for_event - Wait for specific HCI event
 * @expected_event: Expected event code
 * @timeout_ms: Timeout in milliseconds (0 = no timeout)
 * 
 * Returns: 0 on success (event received), negative error on failure
 */
static int hci_wait_for_event(uint8_t expected_event, uint32_t timeout_ms)
{
    uint8_t event_buf[256];
    uint8_t event_code;
    uint8_t params[250];
    uint8_t param_len;
    
    uint32_t start_time = hci_get_uptime_ms();
    uint32_t iterations = 0;
    
    while (1)
    {
        /* Check timeout */
        if (timeout_ms > 0)
        {
            uint32_t elapsed = hci_get_uptime_ms() - start_time;
            if (elapsed >= timeout_ms)
                return -110;  /* ETIMEDOUT */
        }
        
        /* Safety: limit iterations to prevent infinite loops
         * If timeout is 500ms and clock ticks every ~1ms, max iterations is ~500
         * Add some margin for safety */
        iterations++;
        if (iterations > (timeout_ms > 0 ? timeout_ms * 2 : 10000))
        {
            /* Too many iterations - timeout check may not be working */
            return -110;  /* ETIMEDOUT */
        }
        
        /* Check if data is available */
        if (!hci_uart_is_data_available())
            continue;
        
        /* Receive event */
        int ret = hci_uart_receive_event(event_buf, sizeof(event_buf));
        if (ret < 0)
            return ret;
        if (ret == 0)
            continue;
        
        /* Parse event */
        ret = hci_parse_event(event_buf, (size_t)ret, &event_code, params, &param_len);
        if (ret < 0)
            continue;
        
        /* Check if it's the expected event */
        if (event_code == expected_event)
            return 0;
        
        /* Handle specific events */
        if (event_code == HCI_EVENT_COMMAND_COMPLETE)
        {
            /* Command complete - check if it's for our command */
            /* For simplicity, assume any command complete is success */
            if (expected_event == HCI_EVENT_COMMAND_COMPLETE)
                return 0;
        }
        else if (event_code == HCI_EVENT_INQUIRY_RESULT)
        {
            /* Inquiry result - parse and store device */
            if (param_len >= 6)
            {
                /* Basic parsing - BD_ADDR is first 6 bytes */
                bd_addr_t addr;
                memcpy(addr, params, 6);
                
                /* Check if device already exists */
                bool found = false;
                for (int i = 0; i < num_discovered_devices; i++)
                {
                    if (memcmp(discovered_devices[i].bd_addr, addr, 6) == 0)
                    {
                        found = true;
                        discovered_devices[i].last_seen = hci_get_uptime_ms();
                        break;
                    }
                }
                
                /* Add new device if not found and space available */
                if (!found && num_discovered_devices < MAX_BT_DEVICES)
                {
                    struct bluetooth_device *dev = &discovered_devices[num_discovered_devices];
                    memcpy(dev->bd_addr, addr, 6);
                    dev->name[0] = '\0';  /* Name not available yet */
                    dev->discovered = true;
                    dev->last_seen = hci_get_uptime_ms();
                    
                    /* Parse device class if available */
                    if (param_len >= 9)
                    {
                        memcpy(dev->device_class, &params[6], 3);
                    }
                    
                    /* Parse RSSI if available (usually last byte) */
                    if (param_len >= 10)
                    {
                        dev->rssi = (int8_t)params[param_len - 1];
                    }
                    
                    num_discovered_devices++;
                }
            }
        }
    }
}

/**
 * hci_core_init - Initialize HCI core
 */
int hci_core_init(void)
{
    if (hci_dev)
        return 0;  /* Already initialized */
    
    /* Initialize HCI UART transport */
    int ret = hci_uart_init(0x3F8);  /* COM1 */
    if (ret < 0)
        return ret;
    
    /* Allocate HCI device structure */
    hci_dev = (struct hci_device *)kmalloc(sizeof(struct hci_device));
    if (!hci_dev)
        return -ENOMEM;
    
    memset(hci_dev, 0, sizeof(struct hci_device));
    memset(discovered_devices, 0, sizeof(discovered_devices));
    num_discovered_devices = 0;
    
    /* Initialize device */
    hci_dev->initialized = false;
    hci_dev->scanning = false;
    
    return 0;
}

/**
 * hci_reset - Send HCI Reset command
 */
int hci_reset(void)
{
    if (!hci_dev)
        return -ENODEV;
    
    uint8_t cmd_buf[3];
    int len = hci_build_command_packet(HCI_OPCODE_RESET, NULL, 0, cmd_buf, sizeof(cmd_buf));
    if (len < 0)
        return len;
    
    /* Send command */
    int ret = hci_uart_send_command(cmd_buf, (size_t)len);
    if (ret < 0)
        return ret;
    
    /* Wait for command complete with very short timeout
     * Use very short timeout during boot to avoid blocking if no hardware present
     * 100ms is enough to detect if hardware responds quickly
     */
    ret = hci_wait_for_event(HCI_EVENT_COMMAND_COMPLETE, 100);  /* 100ms timeout - quick fail if no hardware */
    if (ret < 0)
    {
        /* Timeout is OK during boot - hardware may not be present */
        return ret;
    }
    
    return 0;
}

/**
 * hci_read_bd_addr - Read Bluetooth Device Address
 */
int hci_read_bd_addr(bd_addr_t addr)
{
    if (!hci_dev || !addr)
        return -EINVAL;
    
    uint8_t cmd_buf[3];
    int len = hci_build_command_packet(HCI_OPCODE_READ_BD_ADDR, NULL, 0, cmd_buf, sizeof(cmd_buf));
    if (len < 0)
        return len;
    
    /* Send command */
    int ret = hci_uart_send_command(cmd_buf, (size_t)len);
    if (ret < 0)
        return ret;
    
    /* Wait for command complete and get BD_ADDR */
    /* Command Complete format: [event_code][param_len][num_packets][opcode_lsb][opcode_msb][status][BD_ADDR...] */
    uint8_t event_buf[256];
    
    /* Process events until we get Command Complete */
    uint32_t start_time = hci_get_uptime_ms();
    while ((hci_get_uptime_ms() - start_time) < 5000)
    {
        if (!hci_uart_is_data_available())
            continue;
        
        int ret2 = hci_uart_receive_event(event_buf, sizeof(event_buf));
        if (ret2 > 0 && event_buf[0] == HCI_EVENT_COMMAND_COMPLETE)
        {
            /* Check if this is for Read_BD_ADDR (opcode in bytes 3-4) */
            uint16_t cmd_opcode = event_buf[3] | (event_buf[4] << 8);
            if (cmd_opcode == HCI_OPCODE_READ_BD_ADDR && ret2 >= 12)
            {
                /* BD_ADDR is in bytes 6-11 */
                memcpy(addr, &event_buf[6], 6);
                return 0;
            }
        }
    }
    
    /* Timeout or parsing failed - return zero address */
    memset(addr, 0, 6);
    return -110;  /* ETIMEDOUT */
}

/**
 * hci_set_event_mask - Set HCI event mask
 */
int hci_set_event_mask(const uint8_t *mask)
{
    if (!mask)
        return -EINVAL;
    
    uint8_t cmd_buf[11];
    int len = hci_build_command_packet(HCI_OPCODE_SET_EVENT_MASK, mask, 8, cmd_buf, sizeof(cmd_buf));
    if (len < 0)
        return len;
    
    int ret = hci_uart_send_command(cmd_buf, (size_t)len);
    if (ret < 0)
        return ret;
    
    ret = hci_wait_for_event(HCI_EVENT_COMMAND_COMPLETE, 5000);
    return ret;
}

/**
 * hci_inquiry - Start device discovery
 */
int hci_inquiry(uint8_t duration, uint8_t num_responses)
{
    if (!hci_dev)
        return -ENODEV;
    
    if (hci_dev->scanning)
        return -EBUSY;  /* Already scanning */
    
    /* Limit duration */
    if (duration == 0 || duration > 61)
        duration = 8;  /* Default 10.24 seconds */
    
    uint8_t params[5];
    params[0] = 0x33;  /* LAP (Limited Access Pattern) - General Inquiry */
    params[1] = 0x8b;
    params[2] = 0x9e;
    params[3] = duration;
    params[4] = num_responses;
    
    uint8_t cmd_buf[8];
    int len = hci_build_command_packet(HCI_OPCODE_INQUIRY, params, 5, cmd_buf, sizeof(cmd_buf));
    if (len < 0)
        return len;
    
    int ret = hci_uart_send_command(cmd_buf, (size_t)len);
    if (ret < 0)
        return ret;
    
    hci_dev->scanning = true;
    
    /* Process events asynchronously - scanning will continue */
    hci_process_events();
    
    return 0;
}

/**
 * hci_inquiry_cancel - Cancel device discovery
 */
int hci_inquiry_cancel(void)
{
    if (!hci_dev)
        return -ENODEV;
    
    if (!hci_dev->scanning)
        return 0;  /* Not scanning */
    
    uint8_t cmd_buf[3];
    int len = hci_build_command_packet(HCI_OPCODE_INQUIRY_CANCEL, NULL, 0, cmd_buf, sizeof(cmd_buf));
    if (len < 0)
        return len;
    
    int ret = hci_uart_send_command(cmd_buf, (size_t)len);
    if (ret < 0)
        return ret;
    
    ret = hci_wait_for_event(HCI_EVENT_COMMAND_COMPLETE, 5000);
    if (ret == 0)
        hci_dev->scanning = false;
    
    return ret;
}

/**
 * hci_remote_name_request - Request remote device name
 */
int hci_remote_name_request(const bd_addr_t bd_addr)
{
    if (!bd_addr)
        return -EINVAL;
    
    uint8_t params[10];
    memcpy(params, bd_addr, 6);
    params[6] = 0x02;  /* Page scan repetition mode */
    params[7] = 0x00;  /* Reserved */
    params[8] = 0x00;  /* Clock offset */
    params[9] = 0x00;
    
    uint8_t cmd_buf[13];
    int len = hci_build_command_packet(HCI_OPCODE_REMOTE_NAME_REQUEST, params, 10, cmd_buf, sizeof(cmd_buf));
    if (len < 0)
        return len;
    
    int ret = hci_uart_send_command(cmd_buf, (size_t)len);
    if (ret < 0)
        return ret;
    
    /* Wait for remote name request complete */
    ret = hci_wait_for_event(HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE, 10000);
    if (ret < 0)
        return ret;
    
    /* Parse event and update device name */
    uint8_t event_buf[256];
    uint8_t event_code;
    uint8_t event_params[250];
    uint8_t param_len;
    
    if (hci_uart_is_data_available())
    {
        ret = hci_uart_receive_event(event_buf, sizeof(event_buf));
        if (ret >= 0)
        {
            hci_parse_event(event_buf, (size_t)ret, &event_code, event_params, &param_len);
            if (event_code == HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE && param_len >= 7)
            {
                /* Find device in discovered list and update name */
                for (int i = 0; i < num_discovered_devices; i++)
                {
                    if (memcmp(discovered_devices[i].bd_addr, bd_addr, 6) == 0)
                    {
                        /* Device name starts at byte 7 (after BD_ADDR and status) */
                        size_t name_len = (size_t)(param_len - 7);
                        if (name_len > 247)
                            name_len = 247;
                        memcpy(discovered_devices[i].name, &event_params[7], name_len);
                        discovered_devices[i].name[name_len] = '\0';
                        break;
                    }
                }
            }
        }
    }
    
    return 0;
}

/**
 * hci_get_discovered_devices - Get list of discovered devices
 */
int hci_get_discovered_devices(struct bluetooth_device *devices, int max_devices)
{
    if (!devices || max_devices <= 0)
        return -EINVAL;
    
    int count = (num_discovered_devices < max_devices) ? num_discovered_devices : max_devices;
    memcpy(devices, discovered_devices, (size_t)count * sizeof(struct bluetooth_device));
    
    return count;
}

/**
 * hci_get_device - Get HCI device instance
 */
struct hci_device *hci_get_device(void)
{
    return hci_dev;
}

/**
 * hci_process_events - Process pending HCI events
 */
int hci_process_events(void)
{
    if (!hci_dev)
        return 0;
    
    int events_processed = 0;
    uint8_t event_buf[256];
    
    /* Process available events (non-blocking) */
    while (hci_uart_is_data_available())
    {
        int ret = hci_uart_receive_event(event_buf, sizeof(event_buf));
        if (ret <= 0)
            break;
        
        uint8_t event_code;
        uint8_t params[250];
        uint8_t param_len;
        
        ret = hci_parse_event(event_buf, (size_t)ret, &event_code, params, &param_len);
        if (ret < 0)
            continue;
        
        events_processed++;
        
        /* Handle inquiry complete */
        if (event_code == HCI_EVENT_INQUIRY_COMPLETE)
        {
            hci_dev->scanning = false;
        }
        /* Handle inquiry result (already handled in hci_wait_for_event) */
        else if (event_code == HCI_EVENT_INQUIRY_RESULT)
        {
            /* Process device discovery */
            if (param_len >= 6)
            {
                bd_addr_t addr;
                memcpy(addr, params, 6);
                
                bool found = false;
                for (int i = 0; i < num_discovered_devices; i++)
                {
                    if (memcmp(discovered_devices[i].bd_addr, addr, 6) == 0)
                    {
                        found = true;
                        discovered_devices[i].last_seen = hci_get_uptime_ms();
                        break;
                    }
                }
                
                if (!found && num_discovered_devices < MAX_BT_DEVICES)
                {
                    struct bluetooth_device *dev = &discovered_devices[num_discovered_devices];
                    memcpy(dev->bd_addr, addr, 6);
                    dev->name[0] = '\0';
                    dev->discovered = true;
                    dev->last_seen = hci_get_uptime_ms();
                    
                    if (param_len >= 9)
                        memcpy(dev->device_class, &params[6], 3);
                    if (param_len >= 10)
                        dev->rssi = (int8_t)params[param_len - 1];
                    
                    num_discovered_devices++;
                }
            }
        }
    }
    
    return events_processed;
}

/**
 * hci_clear_discovered_devices - Clear discovered devices list
 */
int hci_clear_discovered_devices(void)
{
    memset(discovered_devices, 0, sizeof(discovered_devices));
    num_discovered_devices = 0;
    return 0;
}
