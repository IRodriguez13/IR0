/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Bluetooth HCI UART Transport Layer Implementation
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Implements H4 protocol for Bluetooth HCI over UART
 */

#include "hci_uart.h"
#include "../serial/serial.h"
#include <stdint.h>
#include <ir0/kmem.h>
#include <string.h>
#include <ir0/errno.h>
#include <arch/common/arch_interface.h>
#include <arch/common/arch_interface.h>

/* HCI UART instance */
static struct hci_uart *hci_uart_instance = NULL;

/**
 * hci_uart_init - Initialize HCI UART transport
 */
int hci_uart_init(uint16_t uart_base)
{
    if (hci_uart_instance)
    {
        /* Already initialized */
        return 0;
    }
    
    /* Allocate HCI UART structure */
    hci_uart_instance = (struct hci_uart *)kmalloc(sizeof(struct hci_uart));
    if (!hci_uart_instance)
    {
        return -ENOMEM;
    }
    
    memset(hci_uart_instance, 0, sizeof(struct hci_uart));
    hci_uart_instance->uart_base = uart_base;
    hci_uart_instance->initialized = true;
    hci_uart_instance->rx_pos = 0;
    hci_uart_instance->tx_pos = 0;
    
    /* UART should already be initialized by serial_init() */
    /* We use COM1 (0x3F8) which is already set up */
    
    return 0;
}

/**
 * hci_uart_send_command - Send HCI command packet
 */
int hci_uart_send_command(const uint8_t *cmd, size_t len)
{
    if (!hci_uart_instance || !hci_uart_instance->initialized)
    {
        return -ENODEV;
    }
    
    if (!cmd || len == 0)
    {
        return -EINVAL;
    }
    
    /* Send packet type byte (H4 protocol) */
    serial_putchar(HCI_PACKET_TYPE_COMMAND);
    
    /* Send command data */
    for (size_t i = 0; i < len; i++)
    {
        serial_putchar(cmd[i]);
    }
    
    return (int)len;
}

/**
 * hci_uart_send_acl - Send HCI ACL data packet
 */
int hci_uart_send_acl(const uint8_t *data, size_t len)
{
    if (!hci_uart_instance || !hci_uart_instance->initialized)
    {
        return -ENODEV;
    }
    
    if (!data || len == 0)
    {
        return -EINVAL;
    }
    
    /* Send packet type byte (H4 protocol) */
    serial_putchar(HCI_PACKET_TYPE_ACL_DATA);
    
    /* Send ACL data */
    for (size_t i = 0; i < len; i++)
    {
        serial_putchar(data[i]);
    }
    
    return (int)len;
}

/**
 * hci_uart_receive_event - Receive HCI event packet
 */
int hci_uart_receive_event(uint8_t *event, size_t max_len)
{
    if (!hci_uart_instance || !hci_uart_instance->initialized)
    {
        return -ENODEV;
    }
    
    if (!event || max_len == 0)
    {
        return -EINVAL;
    }
    
    /* Check if data is available */
    if (!hci_uart_is_data_available())
    {
        return 0;  /* No data available */
    }
    
    /* Read packet type byte (should be 0x04 for events) */
    uint8_t packet_type = (uint8_t)serial_read_char();
    if (packet_type != HCI_PACKET_TYPE_EVENT)
    {
        /* Wrong packet type, discard */
        return -EINVAL;
    }
    
    /* Read event code (first byte of event) */
    if (max_len < 1)
    {
        return -EINVAL;
    }
    
    event[0] = (uint8_t)serial_read_char();
    
    /* Read parameter length (second byte) */
    if (max_len < 2)
    {
        return 1;  /* Only event code read */
    }
    
    uint8_t param_len = (uint8_t)serial_read_char();
    event[1] = param_len;
    
    /* Read parameters */
    size_t total_len = 2 + param_len;
    if (total_len > max_len)
    {
        total_len = max_len;
    }
    
    for (size_t i = 2; i < total_len; i++)
    {
        event[i] = (uint8_t)serial_read_char();
    }
    
    return (int)total_len;
}

/**
 * hci_uart_receive_acl - Receive HCI ACL data packet
 */
int hci_uart_receive_acl(uint8_t *data, size_t max_len)
{
    if (!hci_uart_instance || !hci_uart_instance->initialized)
    {
        return -ENODEV;
    }
    
    if (!data || max_len == 0)
    {
        return -EINVAL;
    }
    
    /* Check if data is available */
    if (!hci_uart_is_data_available())
    {
        return 0;  /* No data available */
    }
    
    /* Read packet type byte (should be 0x02 for ACL) */
    uint8_t packet_type = (uint8_t)serial_read_char();
    if (packet_type != HCI_PACKET_TYPE_ACL_DATA)
    {
        /* Wrong packet type, discard */
        return -EINVAL;
    }
    
    /* ACL packet format: [Handle LSB][Handle MSB + Flags][Length LSB][Length MSB][Data...] */
    if (max_len < 4)
    {
        return -EINVAL;
    }
    
    /* Read ACL header (4 bytes) */
    for (int i = 0; i < 4; i++)
    {
        data[i] = (uint8_t)serial_read_char();
    }
    
    /* Extract data length */
    uint16_t data_len = data[2] | (data[3] << 8);
    
    /* Read ACL data */
    size_t total_len = 4 + data_len;
    if (total_len > max_len)
    {
        total_len = max_len;
    }
    
    for (size_t i = 4; i < total_len; i++)
    {
        data[i] = (uint8_t)serial_read_char();
    }
    
    return (int)total_len;
}

/**
 * hci_uart_is_data_available - Check if data is available
 */
bool hci_uart_is_data_available(void)
{
    if (!hci_uart_instance || !hci_uart_instance->initialized)
    {
        return false;
    }
    
    /* Check UART line status register for data available */
    /* LSR bit 0 = Data Ready */
    #define SERIAL_PORT_COM1 0x3F8
    #define SERIAL_LINE_STATUS_REG 5
    
    return (inb(SERIAL_PORT_COM1 + SERIAL_LINE_STATUS_REG) & 0x01) != 0;
}

/**
 * hci_uart_get_instance - Get HCI UART instance
 */
struct hci_uart *hci_uart_get_instance(void)
{
    return hci_uart_instance;
}

