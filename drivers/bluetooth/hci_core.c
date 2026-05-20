/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: hci_core.c
 * Description: IR0 kernel source/header file
 */

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
#include <ir0/logging.h>
#include <string.h>
#include <ir0/errno.h>
#include <errno.h>
#include <ir0/clock.h>
#include <ir0/serial_io.h>
#include <stdint.h>

/* Helper: Get uptime in milliseconds */
static uint32_t hci_get_uptime_ms(void)
{
    return (uint32_t)clock_get_uptime_milliseconds();
}

/* Maximum number of discovered devices */
#define MAX_BT_DEVICES 32

/* Inquiry Result (0x02): Num_Responses + N * 13-byte records */
#define HCI_INQUIRY_RESULT_RECORD_LEN 13
/* Inquiry Result with RSSI (0x22): Num_Responses + N * 14-byte records */
#define HCI_INQUIRY_RSSI_RECORD_LEN 14

/* HCI device instance */
static struct hci_device *hci_dev = NULL;

/* Discovered devices list */
static struct bluetooth_device discovered_devices[MAX_BT_DEVICES];
static int num_discovered_devices = 0;

static int hci_controller_post_reset(void);

/**
 * hci_ingest_inquiry_device - Merge one inquiry record into the device list
 */
static void hci_ingest_inquiry_device(const uint8_t *rec, size_t rec_len, int8_t rssi)
{
    bd_addr_t addr;
    bool found;
    struct bluetooth_device *dev;

    if (!rec || rec_len < 6)
        return;

    memcpy(addr, rec, 6);

    found = false;
    for (int j = 0; j < num_discovered_devices; j++)
    {
        if (memcmp(discovered_devices[j].bd_addr, addr, 6) == 0)
        {
            found = true;
            discovered_devices[j].last_seen = hci_get_uptime_ms();
            if (rec_len >= 11)
                memcpy(discovered_devices[j].device_class, &rec[8], 3);
            if (rec_len >= HCI_INQUIRY_RSSI_RECORD_LEN)
                discovered_devices[j].rssi = rssi;
            break;
        }
    }

    if (!found && num_discovered_devices < MAX_BT_DEVICES)
    {
        dev = &discovered_devices[num_discovered_devices];
        memset(dev, 0, sizeof(*dev));
        memcpy(dev->bd_addr, addr, 6);
        if (rec_len >= 11)
            memcpy(dev->device_class, &rec[8], 3);
        dev->discovered = true;
        dev->last_seen = hci_get_uptime_ms();
        dev->rssi = (rec_len >= HCI_INQUIRY_RSSI_RECORD_LEN) ? rssi : 0;
        num_discovered_devices++;
        LOG_INFO_FMT("BLUETOOTH",
                     "Device discovered: %02X:%02X:%02X:%02X:%02X:%02X",
                     dev->bd_addr[0], dev->bd_addr[1], dev->bd_addr[2],
                     dev->bd_addr[3], dev->bd_addr[4], dev->bd_addr[5]);
    }
}

/**
 * hci_ingest_inquiry_result - Parse legacy HCI Inquiry Result event payload
 */
static void hci_ingest_inquiry_result(const uint8_t *params, uint8_t param_len)
{
    uint8_t n;
    size_t off;

    if (!params || param_len < 1)
        return;

    n = params[0];
    if (n == 0)
        return;

    off = 1;
    for (uint8_t i = 0; i < n; i++)
    {
        const uint8_t *rec;

        if (off + HCI_INQUIRY_RESULT_RECORD_LEN > param_len)
            break;

        rec = &params[off];
        hci_ingest_inquiry_device(rec, HCI_INQUIRY_RESULT_RECORD_LEN, 0);
        off += HCI_INQUIRY_RESULT_RECORD_LEN;
    }
}

/**
 * hci_ingest_inquiry_result_with_rssi - Parse HCI 0x22 inquiry payload
 */
static void hci_ingest_inquiry_result_with_rssi(const uint8_t *params, uint8_t param_len)
{
    uint8_t n;
    size_t off;

    if (!params || param_len < 1)
        return;

    n = params[0];
    if (n == 0)
        return;

    off = 1;
    for (uint8_t i = 0; i < n; i++)
    {
        const uint8_t *rec;
        int8_t rssi;

        if (off + HCI_INQUIRY_RSSI_RECORD_LEN > param_len)
            break;

        rec = &params[off];
        rssi = (int8_t)rec[13];
        hci_ingest_inquiry_device(rec, HCI_INQUIRY_RSSI_RECORD_LEN, rssi);
        off += HCI_INQUIRY_RSSI_RECORD_LEN;
    }
}

/**
 * hci_handle_event - Dispatch one parsed HCI event
 */
static void hci_handle_event(uint8_t event_code, const uint8_t *params, uint8_t param_len)
{
    if (!hci_dev)
        return;

    if (event_code == HCI_EVENT_INQUIRY_COMPLETE)
    {
        hci_dev->scanning = false;
        LOG_INFO_FMT("BLUETOOTH", "Inquiry complete, %d device(s) found", num_discovered_devices);
    }
    else if (event_code == HCI_EVENT_INQUIRY_RESULT)
        hci_ingest_inquiry_result(params, param_len);
    else if (event_code == HCI_EVENT_INQUIRY_RESULT_WITH_RSSI)
        hci_ingest_inquiry_result_with_rssi(params, param_len);
}

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
 * hci_wait_command_complete - Wait for Command Complete for a specific opcode
 */
static int hci_wait_command_complete(uint16_t opcode, uint32_t timeout_ms, uint8_t *status_out)
{
    uint8_t event_buf[256];
    uint8_t event_code;
    uint8_t params[250];
    uint8_t param_len;
    uint32_t start_time = hci_get_uptime_ms();
    uint32_t iterations = 0;

    while (1)
    {
        if (timeout_ms > 0)
        {
            uint32_t elapsed = hci_get_uptime_ms() - start_time;

            if (elapsed >= timeout_ms)
                return -110;
        }

        iterations++;
        if (iterations > (timeout_ms > 0 ? timeout_ms * 2 : 10000))
            return -110;

        if (!hci_uart_is_data_available())
            continue;

        {
            int ret = hci_uart_receive_event(event_buf, sizeof(event_buf));

            if (ret < 0)
                return ret;
            if (ret == 0)
                continue;

            ret = hci_parse_event(event_buf, (size_t)ret, &event_code, params, &param_len);
            if (ret < 0)
                continue;

            if (event_code == HCI_EVENT_COMMAND_COMPLETE && param_len >= 3)
            {
                uint16_t rsp_opcode = (uint16_t)(params[1] | (params[2] << 8));

                if (rsp_opcode == opcode)
                {
                    if (status_out)
                        *status_out = (param_len >= 4) ? params[3] : 0xFF;
                    return 0;
                }
            }

            hci_handle_event(event_code, params, param_len);
        }
    }
}

/**
 * hci_controller_post_reset - Finish controller setup after HCI Reset
 */
static int hci_controller_post_reset(void)
{
    uint8_t mask[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x1F};
    bd_addr_t addr;
    int ret;

    ret = hci_set_event_mask(mask);
    if (ret < 0)
        return ret;

    ret = hci_read_bd_addr(addr);
    if (ret == 0)
        memcpy(hci_dev->bd_addr, addr, 6);

    hci_dev->initialized = true;
    return 0;
}

/**
 * hci_core_init - Initialize HCI core
 */
int hci_core_init(void)
{
    if (hci_dev)
        return 0;  /* Already initialized */
    
    /* Initialize HCI UART transport */
    int ret = hci_uart_init(SERIAL_PORT_COM1);
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
    uint8_t status = 0xFF;
    int ret;

    if (!hci_dev)
        return -ENODEV;

    {
        uint8_t cmd_buf[3];
        int len = hci_build_command_packet(HCI_OPCODE_RESET, NULL, 0, cmd_buf, sizeof(cmd_buf));

        if (len < 0)
            return len;

        LOG_INFO("BLUETOOTH", "HCI Reset command sent");
        ret = hci_uart_send_command(cmd_buf, (size_t)len);
        if (ret < 0)
            return ret;
    }

    ret = hci_wait_command_complete(HCI_OPCODE_RESET, 100, &status);
    if (ret < 0)
    {
        LOG_INFO("BLUETOOTH", "HCI Reset: no reply (timeout or no hardware)");
        return ret;
    }

    if (status != HCI_STATUS_SUCCESS)
    {
        LOG_INFO("BLUETOOTH", "HCI Reset failed (controller status)");
        return -EIO;
    }

    LOG_INFO("BLUETOOTH", "HCI Reset complete");
    return hci_controller_post_reset();
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
            if (cmd_opcode == HCI_OPCODE_READ_BD_ADDR && ret2 >= 12 &&
                event_buf[5] == HCI_STATUS_SUCCESS)
            {
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
    uint8_t status;

    if (!mask)
        return -EINVAL;

    {
        uint8_t cmd_buf[11];
        int len = hci_build_command_packet(HCI_OPCODE_SET_EVENT_MASK, mask, 8, cmd_buf, sizeof(cmd_buf));
        int ret;

        if (len < 0)
            return len;

        ret = hci_uart_send_command(cmd_buf, (size_t)len);
        if (ret < 0)
            return ret;
    }

    {
        int ret;

        status = HCI_STATUS_SUCCESS;
        ret = hci_wait_command_complete(HCI_OPCODE_SET_EVENT_MASK, 5000, &status);
        if (ret < 0)
            return ret;
        if (status != HCI_STATUS_SUCCESS)
            return -EIO;
    }

    return 0;
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
    LOG_INFO_FMT("BLUETOOTH", "Inquiry started (duration=%u*1.28s, max_responses=%u)", (unsigned)duration, (unsigned)num_responses);
    hci_process_events();
    return 0;
}

/**
 * hci_inquiry_cancel - Cancel device discovery
 */
int hci_inquiry_cancel(void)
{
    uint8_t status;

    if (!hci_dev)
        return -ENODEV;

    if (!hci_dev->scanning)
        return 0;

    {
        uint8_t cmd_buf[3];
        int len = hci_build_command_packet(HCI_OPCODE_INQUIRY_CANCEL, NULL, 0, cmd_buf,
                                            sizeof(cmd_buf));
        int ret;

        if (len < 0)
            return len;

        ret = hci_uart_send_command(cmd_buf, (size_t)len);
        if (ret < 0)
            return ret;
    }

    {
        int ret;

        status = HCI_STATUS_SUCCESS;
        ret = hci_wait_command_complete(HCI_OPCODE_INQUIRY_CANCEL, 5000, &status);
        if (ret < 0)
            return ret;
        if (status != HCI_STATUS_SUCCESS)
            return -EIO;
    }

    hci_dev->scanning = false;
    LOG_INFO("BLUETOOTH", "Inquiry cancelled");
    return 0;
}

/**
 * hci_remote_name_request - Request remote device name
 */
int hci_remote_name_request(const bd_addr_t bd_addr)
{
    uint8_t event_buf[256];
    uint8_t event_code;
    uint8_t event_params[250];
    uint8_t param_len;
    uint32_t start_time;
    int ret;

    if (!bd_addr)
        return -EINVAL;

    {
        uint8_t params[10];

        memcpy(params, bd_addr, 6);
        params[6] = 0x02;
        params[7] = 0x00;
        params[8] = 0x00;
        params[9] = 0x00;

        {
            uint8_t cmd_buf[13];
            int len = hci_build_command_packet(HCI_OPCODE_REMOTE_NAME_REQUEST, params, 10,
                                               cmd_buf, sizeof(cmd_buf));

            if (len < 0)
                return len;

            ret = hci_uart_send_command(cmd_buf, (size_t)len);
            if (ret < 0)
                return ret;
        }
    }

    start_time = hci_get_uptime_ms();
    while ((hci_get_uptime_ms() - start_time) < 10000)
    {
        if (!hci_uart_is_data_available())
            continue;

        ret = hci_uart_receive_event(event_buf, sizeof(event_buf));
        if (ret <= 0)
            continue;

        ret = hci_parse_event(event_buf, (size_t)ret, &event_code, event_params, &param_len);
        if (ret < 0)
            continue;

        hci_handle_event(event_code, event_params, param_len);

        if (event_code != HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE || param_len < 7)
            continue;

        if (event_params[6] != HCI_STATUS_SUCCESS)
            return -EIO;

        if (memcmp(event_params, bd_addr, 6) != 0)
            continue;

        for (int i = 0; i < num_discovered_devices; i++)
        {
            if (memcmp(discovered_devices[i].bd_addr, bd_addr, 6) == 0)
            {
                size_t name_len = (size_t)(param_len - 7);

                if (name_len > 247)
                    name_len = 247;
                memcpy(discovered_devices[i].name, &event_params[7], name_len);
                discovered_devices[i].name[name_len] = '\0';
                break;
            }
        }

        return 0;
    }

    return -110;
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
        hci_handle_event(event_code, params, param_len);
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
