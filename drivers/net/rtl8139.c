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

#include <ir0/net.h>
#include "rtl8139.h"
#include <stdbool.h>
#include <interrupt/arch/io.h>
#include <ir0/memory/allocator.h>
#include <ir0/memory/kmem.h>
#include <drivers/serial/serial.h>
#include <string.h>
#include <ir0/driver.h>
#include <ir0/logging.h>

/* Global driver state */
static uint16_t rtl8139_io_base = 0;
static uint8_t *rtl8139_rx_buffer = NULL;
static uint32_t rtl8139_current_tx_descriptor = 0;
static uint8_t rtl8139_mac[6];

static struct net_device rtl8139_dev;

/* Forward declarations */
static int32_t rtl8139_hw_init(void);

static ir0_driver_ops_t rtl8139_ops = {
    .init = rtl8139_hw_init,
    .shutdown = NULL
};

static ir0_driver_info_t rtl8139_info = {
    .name = "RTL8139",
    .version = "1.0",
    .author = "Iván Rodriguez",
    .description = "Realtek RTL8139 PCI Fast Ethernet Driver",
    .language = IR0_DRIVER_LANG_C
};

/* Forward declarations for net_device ops */
static int rtl8139_netdev_send(struct net_device *dev, void *data, size_t len);

/* PCI configuration space access */
static uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xfc) | ((uint32_t)PCI_ENABLE_BIT));
    outl(PCI_CONFIG_ADDRESS_PORT, address);
    return inl(PCI_CONFIG_DATA_PORT);
}

static void pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t data)
{
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xfc) | ((uint32_t)PCI_ENABLE_BIT));
    outl(PCI_CONFIG_ADDRESS_PORT, address);
    outl(PCI_CONFIG_DATA_PORT, data);
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
                *bus = (uint8_t)b;
                *slot = s;
                return 0;
            }
        }
    }
    return -1;
}

/**
 * rtl8139_init - register RTL8139 driver
 */
int rtl8139_init(void)
{
    LOG_INFO("RTL8139", "Registering RTL8139 driver...");
    ir0_register_driver(&rtl8139_info, &rtl8139_ops);
    return 0;
}

static int32_t rtl8139_hw_init(void)
{
    uint8_t bus, slot;

    LOG_INFO("RTL8139", "Searching for device...");

    if (find_rtl8139(&bus, &slot) != 0)
    {
        LOG_WARNING("RTL8139", "Device not found");
        return -1;
    }

    LOG_INFO_FMT("RTL8139", "Found device at PCI %d:%d", (int)bus, (int)slot);

    /* Read BAR0 to get I/O base address */
    uint32_t bar0 = pci_config_read(bus, slot, 0, PCI_REG_BAR0);
    if (!(bar0 & 1))
    {
        LOG_ERROR_FMT("RTL8139", "BAR0 is not I/O space. BAR0=0x%x", bar0);
        return -1;
    }
    rtl8139_io_base = bar0 & ~0x3;
    LOG_INFO_FMT("RTL8139", "I/O Base address: 0x%x", rtl8139_io_base);

    /* Enable PCI Bus Mastering and I/O Space */
    uint32_t command = pci_config_read(bus, slot, 0, PCI_REG_COMMAND);
    command |= (PCI_CMD_IO_SPACE | PCI_CMD_BUS_MASTER);
    pci_config_write(bus, slot, 0, PCI_REG_COMMAND, command);

    /* Software Reset */
    outb(rtl8139_io_base + RTL8139_REG_CR, RTL8139_CR_RST);
    while ((inb(rtl8139_io_base + RTL8139_REG_CR) & RTL8139_CR_RST))
    {
        /* Wait for reset to complete */
    }
    LOG_INFO("RTL8139", "Software reset complete.");

    /* Read MAC address (REAL DATA) */
    for (int i = 0; i < 6; i++)
    {
        rtl8139_mac[i] = inb(rtl8139_io_base + RTL8139_REG_MAC0 + i);
    }
    LOG_INFO_FMT("RTL8139", "MAC Address: %02x:%02x:%02x:%02x:%02x:%02x",
                 rtl8139_mac[0], rtl8139_mac[1], rtl8139_mac[2],
                 rtl8139_mac[3], rtl8139_mac[4], rtl8139_mac[5]);

    /* Initialize RX buffer */
    rtl8139_rx_buffer = (uint8_t *)kmalloc(RTL8139_RX_BUF_SIZE + RTL8139_RX_BUF_PADDING);
    memset(rtl8139_rx_buffer, 0, RTL8139_RX_BUF_SIZE + RTL8139_RX_BUF_PADDING);
    outl(rtl8139_io_base + RTL8139_REG_RBSTART, (uint32_t)(uintptr_t)rtl8139_rx_buffer);

    /* Configure interrupts */
    outw(rtl8139_io_base + RTL8139_REG_IMR, RTL8139_INT_ROK | RTL8139_INT_TOK);

    /* Configure RX: Accept Broadcast + Multicast + My Physical, Wrap RX buffer */
    outl(rtl8139_io_base + RTL8139_REG_RCR, RTL8139_RCR_AB | RTL8139_RCR_AM | RTL8139_RCR_APM | RTL8139_RCR_WRAP);

    /* Enable TX and RX */
    outb(rtl8139_io_base + RTL8139_REG_CR, RTL8139_CR_TE | RTL8139_CR_RE);

    /* Read Link Status (REAL DATA) */
    uint8_t msr = inb(rtl8139_io_base + RTL8139_REG_MSR);
    bool link_ok = (msr & RTL8139_MSR_LINKB) != 0;

    /* Register with network stack */
    memset(&rtl8139_dev, 0, sizeof(rtl8139_dev));
    rtl8139_dev.name = "eth0";
    memcpy(rtl8139_dev.mac, rtl8139_mac, 6);
    rtl8139_dev.mtu = 1500; /* Standard Ethernet MTU */
    rtl8139_dev.flags = IFF_BROADCAST;
    if (link_ok)
    {
        rtl8139_dev.flags |= IFF_UP | IFF_RUNNING;
        LOG_INFO("RTL8139", "Link is UP");
    }
    else
    {
        LOG_WARNING("RTL8139", "Link is DOWN");
    }
    
    rtl8139_dev.send = rtl8139_netdev_send;

    net_register_device(&rtl8139_dev);

    LOG_INFO("RTL8139", "Initialization successful.");
    return 0;
}

/**
 * rtl8139_send - Send data through RTL8139 hardware
 * @data: Pointer to data buffer to send
 * @len: Length of data to send
 * 
 * This is the low-level hardware send function. It directly interfaces
 * with the RTL8139 hardware registers.
 */
void rtl8139_send(void *data, size_t len)
{
    if (!data || len == 0 || len > RTL8139_MAX_TX_SIZE)
        return;

    /* Copy data to TX buffer (must be aligned, ideally) */
    /* This driver currently assumes fixed physical buffers or direct map */
    outl(rtl8139_io_base + RTL8139_REG_TSAD0 + (rtl8139_current_tx_descriptor * 4), (uint32_t)(uintptr_t)data);
    outl(rtl8139_io_base + RTL8139_REG_TSD0 + (rtl8139_current_tx_descriptor * 4), (uint32_t)len);

    rtl8139_current_tx_descriptor = (rtl8139_current_tx_descriptor + 1) % 4;
}

/**
 * rtl8139_netdev_send - Network device send wrapper
 * @dev: Network device structure (unused, kept for API compatibility)
 * @data: Pointer to data buffer to send
 * @len: Length of data to send
 * 
 * This wrapper adapts the net_device API to the rtl8139_send() function.
 * It provides the interface expected by the network stack.
 */
static int rtl8139_netdev_send(struct net_device *dev, void *data, size_t len)
{
    (void)dev;

    if (len > RTL8139_MAX_TX_SIZE)
        return -1;

    rtl8139_send(data, len);
    return 0;
}
