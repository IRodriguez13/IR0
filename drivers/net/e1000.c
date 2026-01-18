/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: e1000.c
 * Description: Intel e1000 network card driver implementation
 */

#include "e1000.h"
#include <ir0/net.h>
#include <stdbool.h>
#include <interrupt/arch/io.h>
#include <mm/allocator.h>
#include <ir0/kmem.h>
#include <drivers/serial/serial.h>
#include <string.h>
#include <ir0/driver.h>
#include <ir0/logging.h>

/* Global driver state */
static volatile uint32_t *e1000_mmio_base = NULL;
static uint64_t e1000_mmio_phys_base = 0;
static struct e1000_tx_desc *e1000_tx_ring = NULL;
static struct e1000_rx_desc *e1000_rx_ring = NULL;
static uint8_t *e1000_tx_buffers[E1000_TX_RING_SIZE] = {0};
static uint8_t *e1000_rx_buffers[E1000_RX_RING_SIZE] = {0};
static uint32_t e1000_tx_tail = 0;
static uint32_t e1000_rx_tail = 0;
static uint8_t e1000_mac[6];

static struct net_device e1000_dev;

/* Forward declarations */
static int32_t e1000_hw_init(void);

static ir0_driver_ops_t e1000_ops = {
    .init = e1000_hw_init,
    .shutdown = NULL
};

static ir0_driver_info_t e1000_info = {
    .name = "e1000",
    .version = "1.0",
    .author = "Iván Rodriguez",
    .description = "Intel e1000 PCI Gigabit Ethernet Driver",
    .language = IR0_DRIVER_LANG_C
};

/* Forward declarations for net_device ops */
static int e1000_netdev_send(struct net_device *dev, void *data, size_t len);

/* MMIO register access */
static inline uint32_t e1000_read32(uint32_t reg)
{
    return e1000_mmio_base[reg / 4];
}

static inline void e1000_write32(uint32_t reg, uint32_t value)
{
    e1000_mmio_base[reg / 4] = value;
}

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
static int find_e1000(uint8_t *bus, uint8_t *slot)
{
    /* List of supported e1000 device IDs */
    uint16_t device_ids[] = {
        E1000_DEVICE_ID_82540EM,
        E1000_DEVICE_ID_82545EM,
        E1000_DEVICE_ID_82541PI,
        E1000_DEVICE_ID_82541EI,
        E1000_DEVICE_ID_82546EB,
        E1000_DEVICE_ID_82547EI,
        0 /* Terminator */
    };

    for (uint16_t b = 0; b < 256; b++)
    {
        for (uint8_t s = 0; s < 32; s++)
        {
            uint32_t id = pci_config_read(b, s, 0, 0);
            uint16_t vendor = id & 0xFFFF;
            uint16_t device = id >> 16;

            if (vendor == E1000_VENDOR_ID)
            {
                /* Check if device ID is in our supported list */
                for (int i = 0; device_ids[i] != 0; i++)
                {
                    if (device == device_ids[i])
                    {
                        *bus = (uint8_t)b;
                        *slot = s;
                        return 0;
                    }
                }
            }
        }
    }
    return -1;
}

/**
 * e1000_init - register e1000 driver
 */
int e1000_init(void)
{
    LOG_INFO("e1000", "Registering e1000 driver...");
    ir0_register_driver(&e1000_info, &e1000_ops);
    return 0;
}

static int32_t e1000_hw_init(void)
{
    uint8_t bus, slot;

    LOG_INFO("e1000", "Searching for device...");

    if (find_e1000(&bus, &slot) != 0)
    {
        LOG_INFO("e1000", "Device not found (this is normal if no e1000 hardware is present)");
        return 0; /* Return success - device not found is not an error, just means no hardware */
    }

    LOG_INFO_FMT("e1000", "Found device at PCI %d:%d", (int)bus, (int)slot);

    /* Read BAR0 to get MMIO base address */
    uint32_t bar0 = pci_config_read(bus, slot, 0, PCI_REG_BAR0);
    if (bar0 & 1)
    {
        LOG_ERROR("e1000", "BAR0 is I/O space, but e1000 requires MMIO");
        return -1;
    }
    
    e1000_mmio_phys_base = (uint64_t)(bar0 & ~0xF);
    LOG_INFO_FMT("e1000", "MMIO Physical Base address: 0x%lx", e1000_mmio_phys_base);

    /* Map MMIO to virtual address (identity map)
     * In x86-64, physical addresses below 4GB are identity mapped at boot
     * For addresses above 4GB, would need explicit page table mapping
     * Current implementation assumes MMIO is in identity-mapped region
     */
    e1000_mmio_base = (volatile uint32_t *)(uintptr_t)e1000_mmio_phys_base;

    /* Enable PCI Bus Mastering and Memory Space */
    uint32_t command = pci_config_read(bus, slot, 0, PCI_REG_COMMAND);
    command |= (PCI_CMD_MEM_SPACE | PCI_CMD_BUS_MASTER);
    pci_config_write(bus, slot, 0, PCI_REG_COMMAND, command);

    /* Software Reset */
    uint32_t ctrl = e1000_read32(E1000_REG_CTRL);
    e1000_write32(E1000_REG_CTRL, ctrl | E1000_CTRL_RST);
    
    /* Wait for reset to complete (max 100ms) */
    int timeout = 100000;
    while (e1000_read32(E1000_REG_CTRL) & E1000_CTRL_RST)
    {
        if (--timeout == 0)
        {
            LOG_ERROR("e1000", "Reset timeout");
            return -1;
        }
        /* Small delay */
        for (volatile int i = 0; i < 1000; i++);
    }
    LOG_INFO("e1000", "Software reset complete");

    /* Read MAC address from RAL/RAH registers */
    uint32_t ral = e1000_read32(E1000_REG_RAL);
    uint32_t rah = e1000_read32(E1000_REG_RAH);
    
    e1000_mac[0] = (ral >> 0) & 0xFF;
    e1000_mac[1] = (ral >> 8) & 0xFF;
    e1000_mac[2] = (ral >> 16) & 0xFF;
    e1000_mac[3] = (ral >> 24) & 0xFF;
    e1000_mac[4] = (rah >> 0) & 0xFF;
    e1000_mac[5] = (rah >> 8) & 0xFF;
    
    LOG_INFO_FMT("e1000", "MAC Address: %02x:%02x:%02x:%02x:%02x:%02x",
                 e1000_mac[0], e1000_mac[1], e1000_mac[2],
                 e1000_mac[3], e1000_mac[4], e1000_mac[5]);

    /* Allocate TX descriptor ring */
    e1000_tx_ring = (struct e1000_tx_desc *)kmalloc(sizeof(struct e1000_tx_desc) * E1000_TX_RING_SIZE);
    if (!e1000_tx_ring)
    {
        LOG_ERROR("e1000", "Failed to allocate TX ring");
        return -1;
    }
    memset(e1000_tx_ring, 0, sizeof(struct e1000_tx_desc) * E1000_TX_RING_SIZE);

    /* Allocate TX buffers */
    for (int i = 0; i < E1000_TX_RING_SIZE; i++)
    {
        e1000_tx_buffers[i] = (uint8_t *)kmalloc(E1000_TX_BUFFER_SIZE);
        if (!e1000_tx_buffers[i])
        {
            LOG_ERROR("e1000", "Failed to allocate TX buffer");
            return -1;
        }
        e1000_tx_ring[i].buffer_addr = (uint64_t)(uintptr_t)e1000_tx_buffers[i];
    }

    /* Allocate RX descriptor ring */
    e1000_rx_ring = (struct e1000_rx_desc *)kmalloc(sizeof(struct e1000_rx_desc) * E1000_RX_RING_SIZE);
    if (!e1000_rx_ring)
    {
        LOG_ERROR("e1000", "Failed to allocate RX ring");
        return -1;
    }
    memset(e1000_rx_ring, 0, sizeof(struct e1000_rx_desc) * E1000_RX_RING_SIZE);

    /* Allocate RX buffers */
    for (int i = 0; i < E1000_RX_RING_SIZE; i++)
    {
        e1000_rx_buffers[i] = (uint8_t *)kmalloc(E1000_RX_BUFFER_SIZE);
        if (!e1000_rx_buffers[i])
        {
            LOG_ERROR("e1000", "Failed to allocate RX buffer");
            return -1;
        }
        e1000_rx_ring[i].buffer_addr = (uint64_t)(uintptr_t)e1000_rx_buffers[i];
        e1000_rx_ring[i].status = 0; /* Clear status to indicate ready for receive */
    }

    /* Setup TX descriptors */
    uint64_t tx_ring_phys = (uint64_t)(uintptr_t)e1000_tx_ring;
    e1000_write32(E1000_REG_TDBAL, (uint32_t)(tx_ring_phys & 0xFFFFFFFF));
    e1000_write32(E1000_REG_TDBAH, (uint32_t)(tx_ring_phys >> 32));
    e1000_write32(E1000_REG_TDLEN, E1000_TX_RING_SIZE * sizeof(struct e1000_tx_desc));
    e1000_write32(E1000_REG_TDH, 0);
    e1000_write32(E1000_REG_TDT, 0);
    e1000_tx_tail = 0;

    /* Setup RX descriptors */
    uint64_t rx_ring_phys = (uint64_t)(uintptr_t)e1000_rx_ring;
    e1000_write32(E1000_REG_RDBAL, (uint32_t)(rx_ring_phys & 0xFFFFFFFF));
    e1000_write32(E1000_REG_RDBAH, (uint32_t)(rx_ring_phys >> 32));
    e1000_write32(E1000_REG_RDLEN, E1000_RX_RING_SIZE * sizeof(struct e1000_rx_desc));
    e1000_write32(E1000_REG_RDH, 0);
    e1000_write32(E1000_REG_RDT, E1000_RX_RING_SIZE - 1);
    e1000_rx_tail = E1000_RX_RING_SIZE - 1;

    /* Configure Transmit Control */
    uint32_t tctl = E1000_TCTL_EN | E1000_TCTL_PSP |
                    (E1000_TCTL_CT_SHIFT << 0x10) | /* Collision threshold */
                    (E1000_TCTL_COLD_SHIFT << 0x0C); /* Collision distance */
    e1000_write32(E1000_REG_TCTL, tctl);

    /* Configure Transmit IPG */
    e1000_write32(E1000_REG_TIPG, 0x0060200A); /* Default values */

    /* Configure Receive Control */
    uint32_t rctl = E1000_RCTL_EN |                    /* Enable */
                    E1000_RCTL_BAM |                   /* Broadcast Accept */
                    (0 << E1000_RCTL_BSIZE_SHIFT) |    /* 2048 byte buffer size */
                    E1000_RCTL_SECRC;                  /* Strip Ethernet CRC */
    e1000_write32(E1000_REG_RCTL, rctl);

    /* Enable interrupts */
    e1000_write32(E1000_REG_IMS, E1000_ICR_TXDW | E1000_ICR_RXT0 | E1000_ICR_RXDMT0);

    /* Clear any pending interrupts */
    e1000_read32(E1000_REG_ICR);

    LOG_INFO("e1000", "Device configured");

    /* Check link status */
    uint32_t status = e1000_read32(E1000_REG_STATUS);
    bool link_up = (status & E1000_STATUS_LU) != 0;

    /* Register with network stack */
    memset(&e1000_dev, 0, sizeof(e1000_dev));
    e1000_dev.name = "eth0";
    memcpy(e1000_dev.mac, e1000_mac, 6);
    e1000_dev.mtu = 1500; /* Standard Ethernet MTU */
    e1000_dev.flags = IFF_BROADCAST;
    if (link_up)
    {
        e1000_dev.flags |= IFF_UP | IFF_RUNNING;
        LOG_INFO("e1000", "Link is UP");
    }
    else
    {
        LOG_WARNING("e1000", "Link is DOWN");
    }
    
    e1000_dev.send = e1000_netdev_send;

    net_register_device(&e1000_dev);

    LOG_INFO("e1000", "Initialization successful");
    return 0;
}

/**
 * e1000_send - Send data through e1000 hardware
 * @data: Pointer to data buffer to send
 * @len: Length of data to send
 */
void e1000_send(void *data, size_t len)
{
    if (!data || len == 0 || len > E1000_TX_BUFFER_SIZE || !e1000_tx_ring || !e1000_tx_buffers[e1000_tx_tail])
        return;

    /* Get current TX descriptor */
    uint32_t tx_index = e1000_tx_tail;
    struct e1000_tx_desc *tx_desc = &e1000_tx_ring[tx_index];

    /* Wait for previous transmission to complete */
    uint32_t tdh = e1000_read32(E1000_REG_TDH);
    int timeout = 10000;
    while ((e1000_tx_tail + 1) % E1000_TX_RING_SIZE == tdh)
    {
        if (--timeout == 0)
        {
            LOG_WARNING("e1000", "TX queue full");
            return;
        }
        /* Small delay */
        for (volatile int i = 0; i < 100; i++);
        tdh = e1000_read32(E1000_REG_TDH);
    }

    /* Copy data to TX buffer */
    memcpy(e1000_tx_buffers[tx_index], data, len);

    /* Setup descriptor */
    tx_desc->length = (uint16_t)len;
    tx_desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    tx_desc->status = 0;

    /* Update tail to trigger transmission */
    e1000_tx_tail = (e1000_tx_tail + 1) % E1000_TX_RING_SIZE;
    e1000_write32(E1000_REG_TDT, e1000_tx_tail);
}

/**
 * e1000_netdev_send - Network device send wrapper
 * @dev: Network device structure
 * @data: Pointer to data buffer to send
 * @len: Length of data to send
 */
static int e1000_netdev_send(struct net_device *dev, void *data, size_t len)
{
    (void)dev;

    if (len > E1000_TX_BUFFER_SIZE)
        return -1;

    e1000_send(data, len);
    return 0;
}

void e1000_handle_interrupt(void)
{
    /* Read interrupt cause */
    uint32_t icr = e1000_read32(E1000_REG_ICR);

    if (icr & E1000_ICR_TXDW)
    {
        /* Transmit descriptor written back - transmission complete */
        /* Could check TX descriptors here to free buffers */
    }

    if (icr & E1000_ICR_RXT0 || icr & E1000_ICR_RXDMT0)
    {
        /* RX interrupt - process received packets */
        uint32_t rdh = e1000_read32(E1000_REG_RDH);
        uint32_t tail = (e1000_rx_tail + 1) % E1000_RX_RING_SIZE;
        
        while (tail != rdh)
        {
            struct e1000_rx_desc *rx_desc = &e1000_rx_ring[tail];
            
            if (rx_desc->status & E1000_RXD_STAT_DD)
            {
                if (rx_desc->status & E1000_RXD_STAT_EOP)
                {
                    /* End of packet - process it */
                    if (!(rx_desc->status & (E1000_RXD_STAT_CE | E1000_RXD_STAT_SE | E1000_RXD_STAT_SEQ | E1000_RXD_STAT_RXE)))
                    {
                        /* Packet is valid, pass to network stack */
                        size_t packet_len = rx_desc->length - 4; /* Subtract CRC */
                        net_receive(&e1000_dev, e1000_rx_buffers[tail], packet_len);
                    }
                }
                
                /* Mark descriptor as ready for reuse */
                rx_desc->status = 0;
            }
            
            tail = (tail + 1) % E1000_RX_RING_SIZE;
        }
        
        /* Update RX tail */
        e1000_rx_tail = (tail - 1) % E1000_RX_RING_SIZE;
        e1000_write32(E1000_REG_RDT, e1000_rx_tail);
    }

    if (icr & E1000_ICR_LSC)
    {
        /* Link status change */
        uint32_t status = e1000_read32(E1000_REG_STATUS);
        bool link_up = (status & E1000_STATUS_LU) != 0;
        if (link_up)
        {
            e1000_dev.flags |= IFF_UP | IFF_RUNNING;
            LOG_INFO("e1000", "Link is UP");
        }
        else
        {
            e1000_dev.flags &= ~(IFF_UP | IFF_RUNNING);
            LOG_INFO("e1000", "Link is DOWN");
        }
    }
}

void e1000_get_mac(uint8_t mac[6])
{
    if (mac)
        memcpy(mac, e1000_mac, 6);
}

