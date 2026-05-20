/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: hci_uart.c
 * Description: IR0 kernel Implements H4 protocol for Bluetooth HCI over UART
 */

#include "hci_uart.h"
#include <interrupt/arch/io.h>
#include <stdint.h>
#include <ir0/kmem.h>
#include <string.h>
#include <ir0/errno.h>

#define SERIAL_LINE_STATUS_REG 5
#define SERIAL_DATA_REG        0

/* HCI UART instance */
static struct hci_uart *hci_uart_instance = NULL;

static int hci_uart_try_read_byte(uint8_t *out)
{
    uint16_t base;

    if (!hci_uart_instance || !hci_uart_instance->initialized)
        return 0;

    base = hci_uart_instance->uart_base;

    if ((inb(base + SERIAL_LINE_STATUS_REG) & 0x01) == 0)
        return 0;

    *out = (uint8_t)inb(base + SERIAL_DATA_REG);
    return 1;
}

static void hci_uart_rx_reset(struct hci_uart *uart)
{
    uart->rx_asm_len = 0;
    uart->rx_asm_need = 0;
}

/*
 * Accumulate one HCI event frame (H4 type 0x04 already stripped).
 * Layout in rx_asm: [event_code][param_len][params...]
 */
static int hci_uart_rx_pump(struct hci_uart *uart)
{
    while (uart->rx_asm_len < uart->rx_asm_need || uart->rx_asm_need == 0)
    {
        uint8_t byte;

        if (!hci_uart_try_read_byte(&byte))
            break;

        if (uart->rx_asm_need == 0)
        {
            if (byte != HCI_PACKET_TYPE_EVENT)
                continue;

            uart->rx_asm_len = 0;
            uart->rx_asm_need = 2;
            continue;
        }

        if (uart->rx_asm_len >= HCI_UART_RX_ASM_MAX)
        {
            hci_uart_rx_reset(uart);
            return -EINVAL;
        }

        uart->rx_asm[uart->rx_asm_len++] = byte;

        if (uart->rx_asm_len == 2)
        {
            size_t param_len = uart->rx_asm[1];
            size_t total = 2 + param_len;

            if (total > HCI_UART_RX_ASM_MAX)
            {
                hci_uart_rx_reset(uart);
                return -EINVAL;
            }

            uart->rx_asm_need = total;
        }
    }

    if (uart->rx_asm_need > 0 && uart->rx_asm_len >= uart->rx_asm_need)
        return 1;

    return 0;
}

/*
 * hci_uart_init - Initialize HCI UART transport
 */
int hci_uart_init(uint16_t uart_base)
{
    if (hci_uart_instance)
        return 0;

    hci_uart_instance = (struct hci_uart *)kmalloc(sizeof(struct hci_uart));
    if (!hci_uart_instance)
        return -ENOMEM;

    memset(hci_uart_instance, 0, sizeof(struct hci_uart));
    hci_uart_instance->uart_base = uart_base;
    hci_uart_instance->initialized = true;

    return 0;
}

/**
 * hci_uart_send_command - Send HCI command packet
 */
int hci_uart_send_command(const uint8_t *cmd, size_t len)
{
    uint16_t base;

    if (!hci_uart_instance || !hci_uart_instance->initialized)
        return -ENODEV;

    if (!cmd || len == 0)
        return -EINVAL;

    base = hci_uart_instance->uart_base;
    outb(base + SERIAL_DATA_REG, HCI_PACKET_TYPE_COMMAND);

    for (size_t i = 0; i < len; i++)
        outb(base + SERIAL_DATA_REG, cmd[i]);

    return (int)len;
}

/**
 * hci_uart_send_acl - Send HCI ACL data packet
 */
int hci_uart_send_acl(const uint8_t *data, size_t len)
{
    uint16_t base;

    if (!hci_uart_instance || !hci_uart_instance->initialized)
        return -ENODEV;

    if (!data || len == 0)
        return -EINVAL;

    base = hci_uart_instance->uart_base;
    outb(base + SERIAL_DATA_REG, HCI_PACKET_TYPE_ACL_DATA);

    for (size_t i = 0; i < len; i++)
        outb(base + SERIAL_DATA_REG, data[i]);

    return (int)len;
}

/**
 * hci_uart_receive_event - Receive HCI event packet
 */
int hci_uart_receive_event(uint8_t *event, size_t max_len)
{
    int pump_ret;

    if (!hci_uart_instance || !hci_uart_instance->initialized)
        return -ENODEV;

    if (!event || max_len == 0)
        return -EINVAL;

    pump_ret = hci_uart_rx_pump(hci_uart_instance);
    if (pump_ret < 0)
        return pump_ret;

    if (pump_ret == 0)
        return 0;

    {
        size_t frame_len = hci_uart_instance->rx_asm_len;

        if (frame_len > max_len)
            frame_len = max_len;

        memcpy(event, hci_uart_instance->rx_asm, frame_len);
        hci_uart_rx_reset(hci_uart_instance);
        return (int)frame_len;
    }
}

/**
 * hci_uart_receive_acl - Receive HCI ACL data packet
 */
int hci_uart_receive_acl(uint8_t *data, size_t max_len)
{
    if (!hci_uart_instance || !hci_uart_instance->initialized)
        return -ENODEV;

    if (!data || max_len == 0)
        return -EINVAL;

    if (!hci_uart_is_data_available())
        return 0;

    {
        uint8_t packet_type;

        if (!hci_uart_try_read_byte(&packet_type))
            return 0;

        if (packet_type != HCI_PACKET_TYPE_ACL_DATA)
            return -EINVAL;
    }

    if (max_len < 4)
        return -EINVAL;

    for (int i = 0; i < 4; i++)
    {
        uint8_t byte;

        if (!hci_uart_try_read_byte(&byte))
            return -EAGAIN;

        data[i] = byte;
    }

    {
        uint16_t data_len = (uint16_t)data[2] | ((uint16_t)data[3] << 8);
        size_t total_len = 4 + data_len;

        if (total_len > max_len)
            total_len = max_len;

        for (size_t i = 4; i < total_len; i++)
        {
            uint8_t byte;

            if (!hci_uart_try_read_byte(&byte))
                return -EAGAIN;

            data[i] = byte;
        }

        return (int)total_len;
    }
}

/**
 * hci_uart_is_data_available - Check if data is available
 */
bool hci_uart_is_data_available(void)
{
    if (!hci_uart_instance || !hci_uart_instance->initialized)
        return false;

    if (hci_uart_instance->rx_asm_need > 0 &&
        hci_uart_instance->rx_asm_len < hci_uart_instance->rx_asm_need)
        return true;

    return (inb(hci_uart_instance->uart_base + SERIAL_LINE_STATUS_REG) & 0x01) != 0;
}

/**
 * hci_uart_get_instance - Get HCI UART instance
 */
struct hci_uart *hci_uart_get_instance(void)
{
    return hci_uart_instance;
}
