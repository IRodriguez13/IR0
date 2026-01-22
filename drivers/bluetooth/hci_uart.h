/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Bluetooth HCI UART Transport Layer
 * Copyright (C) 2025  Iván Rodriguez
 *
 * HCI (Host Controller Interface) transport over UART
 * Implements H4 protocol for Bluetooth communication
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* HCI Packet Types (H4 protocol) */
#define HCI_PACKET_TYPE_COMMAND    0x01
#define HCI_PACKET_TYPE_ACL_DATA   0x02
#define HCI_PACKET_TYPE_SCO_DATA  0x03
#define HCI_PACKET_TYPE_EVENT      0x04

/* HCI UART structure */
struct hci_uart {
    uint16_t uart_base;      /* UART base address (COM1 = 0x3F8) */
    bool initialized;        /* Initialization status */
    uint8_t rx_buffer[1024]; /* Receive buffer */
    uint8_t tx_buffer[1024]; /* Transmit buffer */
    size_t rx_pos;          /* Current RX buffer position */
    size_t tx_pos;          /* Current TX buffer position */
};

/**
 * hci_uart_init - Initialize HCI UART transport
 * @uart_base: UART base address (0x3F8 for COM1)
 * 
 * Initializes UART for HCI communication using H4 protocol.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int hci_uart_init(uint16_t uart_base);

/**
 * hci_uart_send_command - Send HCI command packet
 * @cmd: Command packet data (without packet type byte)
 * @len: Length of command data
 * 
 * Sends HCI command over UART with H4 framing.
 * Packet format: [0x01][Command Data...]
 * 
 * Returns: Number of bytes sent on success, negative error on failure
 */
int hci_uart_send_command(const uint8_t *cmd, size_t len);

/**
 * hci_uart_send_acl - Send HCI ACL data packet
 * @data: ACL data (without packet type byte)
 * @len: Length of ACL data
 * 
 * Sends ACL data over UART with H4 framing.
 * Packet format: [0x02][ACL Data...]
 * 
 * Returns: Number of bytes sent on success, negative error on failure
 */
int hci_uart_send_acl(const uint8_t *data, size_t len);

/**
 * hci_uart_receive_event - Receive HCI event packet
 * @event: Buffer to store event data (without packet type byte)
 * @max_len: Maximum length of event buffer
 * 
 * Receives HCI event from UART.
 * Packet format: [0x04][Event Data...]
 * 
 * Returns: Number of bytes received on success, 0 if no data, negative error on failure
 */
int hci_uart_receive_event(uint8_t *event, size_t max_len);

/**
 * hci_uart_receive_acl - Receive HCI ACL data packet
 * @data: Buffer to store ACL data (without packet type byte)
 * @max_len: Maximum length of data buffer
 * 
 * Receives ACL data from UART.
 * Packet format: [0x02][ACL Data...]
 * 
 * Returns: Number of bytes received on success, 0 if no data, negative error on failure
 */
int hci_uart_receive_acl(uint8_t *data, size_t max_len);

/**
 * hci_uart_is_data_available - Check if data is available for reading
 * 
 * Returns: true if data is available, false otherwise
 */
bool hci_uart_is_data_available(void);

/**
 * hci_uart_get_instance - Get HCI UART instance
 * 
 * Returns: Pointer to HCI UART instance, NULL if not initialized
 */
struct hci_uart *hci_uart_get_instance(void);



