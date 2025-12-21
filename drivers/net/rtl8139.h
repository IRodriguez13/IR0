/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: rtl8139.h
 * Description: RTL8139 network card driver definitions
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/* PCI definitions for RTL8139 */
#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139

/* RTL8139 Registers */
#define RTL8139_REG_MAC0            0x00    /* MAC Address */
#define RTL8139_REG_MAR0            0x08    /* Multicast Filter */
#define RTL8139_REG_TSD0            0x10    /* Transmit Status Descriptor 0 */
#define RTL8139_REG_TSAD0           0x20    /* Transmit Start Address Descriptor 0 */
#define RTL8139_REG_RBSTART         0x30    /* Receive Buffer Start Address */
#define RTL8139_REG_CR              0x37    /* Command Register */
#define RTL8139_REG_CAPR            0x38    /* Current Address of Packet Read */
#define RTL8139_REG_IMR             0x3C    /* Interrupt Mask Register */
#define RTL8139_REG_ISR             0x3E    /* Interrupt Status Register */
#define RTL8139_REG_TCR             0x40    /* Transmit Configuration Register */
#define RTL8139_REG_RCR             0x44    /* Receive Configuration Register */
#define RTL8139_REG_CONFIG1         0x52    /* Configuration Register 1 */

/* Command Register bits */
#define RTL8139_CR_BUFE             (1 << 0)    /* Buffer Empty */
#define RTL8139_CR_TE               (1 << 2)    /* Transmitter Enable */
#define RTL8139_CR_RE               (1 << 3)    /* Receiver Enable */
#define RTL8139_CR_RST              (1 << 4)    /* Software Reset */

/* Interrupt Status/Mask Register bits */
#define RTL8139_INT_ROK             (1 << 0)    /* Receive OK */
#define RTL8139_INT_RER             (1 << 1)    /* Receive Error */
#define RTL8139_INT_TOK             (1 << 2)    /* Transmit OK */
#define RTL8139_INT_TER             (1 << 3)    /* Transmit Error */

/* Receive Configuration Register bits */
#define RTL8139_RCR_AAP             (1 << 0)    /* Accept All Packets */
#define RTL8139_RCR_APM             (1 << 1)    /* Accept Physical Match */
#define RTL8139_RCR_AM              (1 << 2)    /* Accept Multicast */
#define RTL8139_RCR_AB              (1 << 3)    /* Accept Broadcast */
#define RTL8139_RCR_WRAP            (1 << 7)    /* Wrap around */

/* Transmit Status Descriptor bits */
#define RTL8139_TSD_SIZE_MASK       0x1FFF      /* Packet size mask */
#define RTL8139_TSD_OWN             (1 << 13)   /* DMA operation completed */

/* Public API */
int rtl8139_init(void);
void rtl8139_send(void *data, size_t len);
void rtl8139_handle_interrupt(void);
void rtl8139_get_mac(uint8_t mac[6]);
