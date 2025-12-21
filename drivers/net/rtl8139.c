/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: rtl8139.c
 * Description: RTL8139 network card driver implementation
 */

#include "rtl8139.h"
#include <interrupt/arch/io.h>
#include <ir0/memory/allocator.h>
#include <drivers/serial/serial.h>
#include <string.h>

/* Global driver state */
static uint16_t rtl8139_io_base = 0;
static uint8_t *rtl8139_rx_buffer = NULL;
static uint32_t rtl8139_current_tx_descriptor = 0;
static uint8_t rtl8139_mac[6];

/* PCI configuration space access */
static uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));
    outl(0xCF8, address);
    return inl(0xCFC);
}

static void pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t data)
{
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));
    outl(0xCF8, address);
    outl(0xCFC, data);
}

/* PCI device discovery */
static int find_rtl8139(uint8_t *bus, uint8_t *slot)
{
    for (uint16_t b = 0; b < 256; b++)
    {
        for (uint8_t s = 0; s < 32; s++)
        {
            uint32_t id = pci_config_read(b, s, 0, 0);
            if ((id & 0xFFFF) == RTL8139_VENDOR_ID && (id >> 16) == RTL8139_DEVICE_ID)
            {
                *bus = b;
                *slot = s;
                return 0;
            }
        }
    }
    return -1;
}

int rtl8139_init(void)
{
    uint8_t bus, slot;

    serial_print("RTL8139: Searching for device...\n");
    if (find_rtl8139(&bus, &slot) != 0)
    {
        serial_print("RTL8139: Device NOT found.\n");
        return -1;
    }

    serial_print("RTL8139: Found device at PCI ");
    serial_print_hex32(bus);
    serial_print(":");
    serial_print_hex32(slot);
    serial_print("\n");

    /* Read BAR0 to get I/O base address */
    uint32_t bar0 = pci_config_read(bus, slot, 0, 0x10);
    if (!(bar0 & 1))
    {
        serial_print("RTL8139: BAR0 is not I/O space. BAR0=");
        serial_print_hex32(bar0);
        serial_print("\n");
        return -1;
    }
    rtl8139_io_base = bar0 & ~0x3;
    serial_print("RTL8139: I/O Base address: 0x");
    serial_print_hex32(rtl8139_io_base);
    serial_print("\n");

    /* Enable PCI Bus Mastering and I/O Space */
    uint32_t command = pci_config_read(bus, slot, 0, 0x04);
    command |= (1 << 0) | (1 << 2); /* I/O Space + Bus Master */
    pci_config_write(bus, slot, 0, 0x04, command);

    /* 1. Power on the device (Config1 register) */
    outb(rtl8139_io_base + RTL8139_REG_CONFIG1, 0x00);

    /* 2. Software Reset */
    outb(rtl8139_io_base + RTL8139_REG_CR, RTL8139_CR_RST);
    while (inb(rtl8139_io_base + RTL8139_REG_CR) & RTL8139_CR_RST)
    {
        /* Wait for reset to complete */
    }
    serial_print("RTL8139: Software reset complete.\n");

    /* 3. Allocate and set up Receive Buffer (8KB + 16 bytes + extra for alignment) */
    /* RTL8139 requires the RX buffer to be 4-byte aligned */
    rtl8139_rx_buffer = alloc(8192 + 16 + 1500); /* Some extra space */
    if (!rtl8139_rx_buffer)
    {
        serial_print("RTL8139: Failed to allocate RX buffer.\n");
        return -1;
    }
    memset(rtl8139_rx_buffer, 0, 8192 + 16 + 1500);
    outl(rtl8139_io_base + RTL8139_REG_RBSTART, (uint32_t)(uintptr_t)rtl8139_rx_buffer);

    /* 4. Set Receive Configuration Register (RCR) */
    /* Accept Broadcast, Multicast, Physical match, and let it wrap */
    outl(rtl8139_io_base + RTL8139_REG_RCR, RTL8139_RCR_AB | RTL8139_RCR_AM | RTL8139_RCR_APM | RTL8139_RCR_AAP | RTL8139_RCR_WRAP);

    /* 5. Enable Transmitter and Receiver */
    outb(rtl8139_io_base + RTL8139_REG_CR, RTL8139_CR_TE | RTL8139_CR_RE);

    /* 6. Read MAC Address */
    for (int i = 0; i < 6; i++)
    {
        rtl8139_mac[i] = inb(rtl8139_io_base + RTL8139_REG_MAC0 + i);
    }

    serial_print("RTL8139: MAC Address: ");
    for (int i = 0; i < 6; i++)
    {
        serial_print_hex32(rtl8139_mac[i]);
        if (i < 5)
            serial_print(":");
    }
    serial_print("\n");

    /* 7. Setup Interrupts (Optional for now, we'll poll or use later) */
    /* For now, disable all interrupts and let poll handle things */
    outw(rtl8139_io_base + RTL8139_REG_IMR, 0x0005); /* ROK + TER for testing */

    serial_print("RTL8139: Initialization successful.\n");
    return 0;
}

void rtl8139_send(void *data, size_t len)
{
    if (len > 1792)
    {
        serial_print("RTL8139: Packet too large to send.\n");
        return;
    }

    /* We need a physically contiguous buffer for DMA. alloc() provides this. */
    /* However, for simplicity, we'll just use the memory provided if it's in the identity map. */

    /* Set transmit start address */
    outl(rtl8139_io_base + RTL8139_REG_TSAD0 + (rtl8139_current_tx_descriptor * 4), (uint32_t)(uintptr_t)data);

    /* Set transmit status (this starts the transmission) */
    uint32_t status = (len & RTL8139_TSD_SIZE_MASK);
    outl(rtl8139_io_base + RTL8139_REG_TSD0 + (rtl8139_current_tx_descriptor * 4), status);

    /* Advance descriptor (there are 4) */
    rtl8139_current_tx_descriptor = (rtl8139_current_tx_descriptor + 1) % 4;
}

void rtl8139_get_mac(uint8_t mac[6])
{
    memcpy(mac, rtl8139_mac, 6);
}

void rtl8139_handle_interrupt(void)
{
    uint16_t status = inw(rtl8139_io_base + RTL8139_REG_ISR);

    if (status & RTL8139_INT_ROK)
    {
        serial_print("RTL8139: Packet received!\n");
        /* Packet processing would happen here */
    }

    if (status & RTL8139_INT_TOK)
    {
        /* Transmit OK */
    }

    /* Acknowledge interrupts */
    outw(rtl8139_io_base + RTL8139_REG_ISR, status);
}
