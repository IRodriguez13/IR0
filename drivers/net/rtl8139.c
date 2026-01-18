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
#include <mm/allocator.h>
#include <ir0/kmem.h>
#include <drivers/serial/serial.h>
#include <string.h>
#include <ir0/driver.h>
#include <ir0/logging.h>
#include <ir0/oops.h>  /* For ASSERT and panicex */

/* Global driver state */
static uint16_t rtl8139_io_base = 0;
static uint8_t *rtl8139_rx_buffer = NULL;
static uint8_t *rtl8139_tx_buffers[4] = {NULL, NULL, NULL, NULL}; /* 4 TX descriptors */
static uint32_t rtl8139_current_tx_descriptor = 0;
static uint8_t rtl8139_mac[6];
static uint16_t rtl8139_rx_read_offset = 0; /* Current read offset in RX buffer */

/* TX tracking for robust DMA management */
#define RTL8139_MAX_TX_IN_FLIGHT 4  /* Maximum number of TX packets in flight */
static volatile uint32_t rtl8139_tx_in_flight = 0;  /* Counter of TX packets currently being DMA'd */
static volatile uint32_t rtl8139_tx_descriptor_own_state[4] = {0, 0, 0, 0};  /* Track OWN bit state per descriptor */

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
    rtl8139_rx_read_offset = 0; /* Initialize read offset */

    /* Initialize TX buffers (one per descriptor) */
    /* CRITICAL DMA REQUIREMENTS:
     * 1. Buffers must be 32-bit aligned (4-byte boundary)
     * 2. Buffers must be in physical memory within first 4GB (RTL8139 is 32-bit PCI)
     * 3. Buffers must remain valid until DMA completes (never freed while in use)
     */
    for (int i = 0; i < 4; i++)
    {
        /* Allocate TX buffer - kmalloc should return aligned memory, but verify */
        rtl8139_tx_buffers[i] = (uint8_t *)kmalloc(RTL8139_MAX_TX_SIZE);
        if (!rtl8139_tx_buffers[i])
        {
            LOG_ERROR("RTL8139", "Failed to allocate TX buffer");
            return -1;
        }
        
        /* Verify alignment (must be 32-bit aligned) */
        uintptr_t addr = (uintptr_t)rtl8139_tx_buffers[i];
        if (addr % 4 != 0)
        {
            LOG_ERROR_FMT("RTL8139", "TX buffer %d not 32-bit aligned: %p", i, rtl8139_tx_buffers[i]);
            return -1;
        }
        
        /* Verify physical address is within 32-bit range (first 4GB) */
        uint32_t phys_addr = (uint32_t)addr;
        if (phys_addr > 0xFFFFFFFF)
        {
            LOG_ERROR_FMT("RTL8139", "TX buffer %d physical address > 4GB: 0x%x", i, phys_addr);
            return -1;
        }
        
        /* Register TX buffer PHYSICAL address with hardware */
        /* Hardware uses this address for DMA - must be physical, not virtual */
        outl(rtl8139_io_base + RTL8139_REG_TSAD0 + (i * 4), phys_addr);
        
        /* CRITICAL: Initialize TSD register to 0 (descriptor free, no packet size) */
        /* This ensures OWN=0 so descriptors start in a free state */
        /* If we don't do this, descriptors might have residual OWN=1 from hardware reset */
        outl(rtl8139_io_base + RTL8139_REG_TSD0 + (i * 4), 0);
        
        /* Verify TSD is actually 0 after write */
        uint32_t tsd_init = inl(rtl8139_io_base + RTL8139_REG_TSD0 + (i * 4));
        if (tsd_init != 0)
        {
            LOG_WARNING_FMT("RTL8139", "TX descriptor %d TSD not zero after init: 0x%x", i, tsd_init);
        }
        
        LOG_DEBUG_FMT("RTL8139", "TX buffer %d: virt=%p, phys=0x%x, size=%d, TSD=0x%x", 
                     i, rtl8139_tx_buffers[i], phys_addr, RTL8139_MAX_TX_SIZE, tsd_init);
    }

    /* Configure interrupts */
    outw(rtl8139_io_base + RTL8139_REG_IMR, RTL8139_INT_ROK | RTL8139_INT_TOK);

    /* Configure RX: Accept All Packets (debug) + Broadcast + Multicast + My Physical, Wrap RX buffer */
    /* AAP is CRITICAL for debugging - without it, many responses never enter */
    outl(rtl8139_io_base + RTL8139_REG_RCR, 
         RTL8139_RCR_AAP |  /* Accept all packets (debug) */
         RTL8139_RCR_APM |  /* Accept physical match */
         RTL8139_RCR_AB  |  /* Accept broadcast */
         RTL8139_RCR_AM  |  /* Accept multicast */
         RTL8139_RCR_WRAP); /* Wrap around */

    /* Enable TX and RX */
    outb(rtl8139_io_base + RTL8139_REG_CR, RTL8139_CR_TE | RTL8139_CR_RE);

    /* Read PCI Interrupt Line to get actual IRQ */
    uint32_t pci_int_line = pci_config_read(bus, slot, 0, PCI_REG_INTERRUPT_LINE);
    uint8_t irq = (uint8_t)(pci_int_line & 0xFF);
    LOG_INFO_FMT("RTL8139", "PCI Interrupt Line (IRQ): %d", (int)irq);

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
 * REIMPLEMENTED: Simple, clean version focused on core functionality.
 * This function copies data to a dedicated TX buffer and initiates DMA.
 */
void rtl8139_send(void *data, size_t len)
{
    /* REIMPLEMENTED: Simple, clean version */
    extern void serial_print(const char *);
    extern void serial_print_hex32(uint32_t);
    extern char *itoa(int, char*, int);
    
    /* Basic parameter validation */
    if (!data || len == 0 || len > RTL8139_MAX_TX_SIZE || !rtl8139_io_base) {
        return;
    }
    
    /* Find a free TX descriptor */
    int desc = -1;
    for (int i = 0; i < 4; i++) {
        uint32_t tsd_addr = rtl8139_io_base + RTL8139_REG_TSD0 + (i * 4);
        uint32_t tsd = inl(tsd_addr);
        
        /* Descriptor is free if OWN bit is NOT set */
        if (!(tsd & RTL8139_TSD_OWN)) {
            desc = i;
            break;
        }
    }
    
    if (desc == -1) {
        /* All descriptors busy - drop packet */
        return;
    }
    
    /* Get TX buffer for this descriptor */
    void *tx_buf = rtl8139_tx_buffers[desc];
    if (!tx_buf) {
        return;
    }
    
    /* Copy data to TX buffer */
    memcpy(tx_buf, data, len);
    
    /* Memory barrier to ensure copy completes */
    __asm__ volatile("mfence" ::: "memory");
    
    /* Write TSD to start DMA */
    uint32_t tsd_addr = rtl8139_io_base + RTL8139_REG_TSD0 + (desc * 4);
    uint32_t tsd_value = (uint32_t)len & RTL8139_TSD_SIZE_MASK;
    
    serial_print("[RTL8139] TX: desc=");
    char desc_str[8];
    itoa(desc, desc_str, 10);
    serial_print(desc_str);
    serial_print(" len=");
    char len_str[16];
    itoa((int)len, len_str, 10);
    serial_print(len_str);
    serial_print(" tsd_reg=0x");
    serial_print_hex32(tsd_addr);
    serial_print(" tsd_val=0x");
    serial_print_hex32(tsd_value);
    serial_print("\n");
    
    /* Execute outl - this starts DMA */
    outl((uint16_t)tsd_addr, tsd_value);
    
    /* Memory barrier after I/O */
    __asm__ volatile("mfence" ::: "memory");
    
    /* Update tracking */
    rtl8139_tx_descriptor_own_state[desc] = 1;
    rtl8139_tx_in_flight++;
    rtl8139_current_tx_descriptor = (desc + 1) % 4;
    
    serial_print("[RTL8139] TX: DMA started, in_flight=");
    char flight_str[8];
    itoa((int)rtl8139_tx_in_flight, flight_str, 10);
    serial_print(flight_str);
    serial_print("\n");
}

/**
 * rtl8139_check_tx_completion - Check if any TX descriptors have completed

/**
 * rtl8139_check_tx_completion - Check if any TX descriptors have completed
 * 
 * This function checks all TX descriptors and updates the in-flight counter
 * when hardware finishes DMA (OWN bit changes from 1 to 0).
 * 
 * CRITICAL: This should be called periodically (from interrupt handler or polling)
 * to ensure the in-flight counter stays accurate.
 */
static void rtl8139_check_tx_completion(void)
{
    if (!rtl8139_io_base)
        return;
    
    extern void serial_print(const char *);
    extern void serial_print_hex32(uint32_t);
    extern char *itoa(int, char*, int);
    
    /* Check all 4 descriptors */
    for (int i = 0; i < 4; i++)
    {
        uint32_t reg_addr = rtl8139_io_base + RTL8139_REG_TSD0 + (i * 4);
        
        /* CRITICAL: Memory barrier before reading hardware state */
        __asm__ volatile("mfence" ::: "memory");
        uint32_t tsd = inl(reg_addr);
        __asm__ volatile("mfence" ::: "memory");
        
        uint32_t own = tsd & RTL8139_TSD_OWN;
        uint32_t prev_own = rtl8139_tx_descriptor_own_state[i];
        
        /* Update our tracking */
        rtl8139_tx_descriptor_own_state[i] = own ? 1 : 0;
        
        /* CRITICAL: If OWN changed from 1 to 0, hardware finished DMA */
        if (prev_own == 1 && own == 0)
        {
            /* Hardware finished with this descriptor - decrement in-flight counter */
            if (rtl8139_tx_in_flight > 0)
            {
                rtl8139_tx_in_flight--;
                serial_print("[RTL8139] TX OWN CHECK [RETURNED]: desc=");
                char desc_str[8];
                itoa(i, desc_str, 10);
                serial_print(desc_str);
                serial_print(" OWN changed 1->0, TX in-flight decremented to ");
                char flight_str[8];
                itoa((int)rtl8139_tx_in_flight, flight_str, 10);
                serial_print(flight_str);
                serial_print("\n");
                
                LOG_DEBUG_FMT("RTL8139", "TX descriptor %d completed, in-flight=%d", 
                             i, rtl8139_tx_in_flight);
            }
            else
            {
                /* ASSERT: In-flight counter should never go below 0 */
                serial_print("[RTL8139] TX OWN CHECK [ERROR]: desc=");
                char desc_str[8];
                itoa(i, desc_str, 10);
                serial_print(desc_str);
                serial_print(" OWN changed 1->0 but in-flight counter already 0!\n");
                LOG_WARNING_FMT("RTL8139", "TX descriptor %d completed but in-flight counter already 0", i);
            }
        }
    }
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
    /* CRITICAL: Log parameters IMMEDIATELY to catch corruption */
    extern void serial_print(const char *);
    extern void serial_print_hex32(uint32_t);
    extern char *itoa(int, char*, int);
    
    serial_print("[RTL8139] netdev_send ENTRY: dev=");
    serial_print_hex32((uint32_t)(uintptr_t)dev);
    serial_print(" data=");
    serial_print_hex32((uint32_t)(uintptr_t)data);
    serial_print(" len=");
    char len_str[32];
    itoa((int)len, len_str, 10);
    serial_print(len_str);
    serial_print(" (0x");
    serial_print_hex32((uint32_t)len);
    serial_print(")\n");
    
    (void)dev;

    LOG_INFO_FMT("RTL8139", "netdev_send: data=%p, len=%d (0x%x)", 
                 data, (int)len, (unsigned int)len);
    
    /* CRITICAL: Validate len IMMEDIATELY - if corrupted, abort */
    if (len > RTL8139_MAX_TX_SIZE || len == 0 || len > 2000)
    {
        LOG_ERROR_FMT("RTL8139", "netdev_send: CORRUPTION! len=%d (0x%x) is invalid! MAX=%d", 
                     (int)len, (unsigned int)len, RTL8139_MAX_TX_SIZE);
        serial_print("[RTL8139] ERROR: Invalid len parameter!\n");
        return -1;
    }
    
    if (!data)
    {
        LOG_ERROR("RTL8139", "netdev_send: data is NULL");
        return -1;
    }

    LOG_INFO("RTL8139", "netdev_send: Calling rtl8139_send");
    rtl8139_send(data, len);
    LOG_INFO("RTL8139", "netdev_send: rtl8139_send returned");
    return 0;
}

/**
 * rtl8139_process_rx_packets - Process received packets from RX buffer
 * 
 * This is the core packet processing logic, extracted so it can be called
 * both from interrupt handler and from polling function.
 */
static void rtl8139_process_rx_packets(void)
{
    if (!rtl8139_io_base || !rtl8139_rx_buffer)
    {
        return;
    }

    /* Read hardware write pointer
     * In RTL8139, the write pointer is at offset 0x10 in the RX buffer
     * We need to read it as a 16-bit value in little-endian format
     */
    uint16_t current_write = *((volatile uint16_t *)(rtl8139_rx_buffer + 0x10));
    current_write &= (RTL8139_RX_BUF_SIZE - 1);  /* Mask to buffer size */
    
    /* Handle wrap-around: if current_write < read_offset, we wrapped */
    if (current_write < rtl8139_rx_read_offset)
    {
        /* Buffer wrapped around, process from read_offset to end, then from start to write */
        LOG_DEBUG("RTL8139", "RX buffer wrapped");
    }
    
    /* If write pointer hasn't advanced, there are no new packets */
    if (current_write == rtl8139_rx_read_offset)
    {
        return;
    }
    
    LOG_DEBUG_FMT("RTL8139", "RX buffer: read_offset=%d, write_offset=%d", 
                 rtl8139_rx_read_offset, current_write);
    
    int packet_count = 0;
    /* Process packets from our read offset to current write pointer */
    while (rtl8139_rx_read_offset != current_write)
    {
        /* Read packet header (4 bytes: status, length) */
        uint16_t status = *((uint16_t *)(rtl8139_rx_buffer + rtl8139_rx_read_offset));
        uint16_t length = *((uint16_t *)(rtl8139_rx_buffer + rtl8139_rx_read_offset + 2));
        
        /* Check if packet is valid */
        if (length == 0 || length > RTL8139_RX_BUF_SIZE)
            break;
        
        /* Check if packet is OK */
        if (status & RTL8139_RX_STAT_ROK)
        {
            /* Packet data starts after 4-byte header (status + length) */
            /* The 'length' field contains the Ethernet frame size (NOT including the 4-byte header) */
            void *packet_data = rtl8139_rx_buffer + rtl8139_rx_read_offset + 4;
            size_t packet_len = length; /* length already excludes the 4-byte header */
            
            LOG_DEBUG_FMT("RTL8139", "Packet #%d: status=0x%04x, length=%d, data_len=%d",
                         packet_count, status, length, packet_len);
            
            if (packet_len > 0 && packet_len <= 1518) /* Valid Ethernet frame size */
            {
                /* Debug: Log raw packet info before parsing */
                extern void serial_print_hex32(uint32_t);
                extern char *itoa(int, char*, int);
                serial_print("[RTL8139] RX len=");
                char len_str[16];
                itoa((int)packet_len, len_str, 10);
                serial_print(len_str);
                if (packet_len >= 14) {
                    struct eth_header *eth = (struct eth_header *)packet_data;
                    uint16_t type = ntohs(eth->type);
                    serial_print(" type=0x");
                    serial_print_hex32((uint32_t)type);
                }
                serial_print("\n");
                
                /* Pass packet to network stack */
                net_receive(&rtl8139_dev, packet_data, packet_len);
                packet_count++;
            }
            else
            {
                LOG_WARNING_FMT("RTL8139", "Invalid packet length: %d", packet_len);
            }
        }
        else
        {
            LOG_WARNING_FMT("RTL8139", "Packet with bad status: 0x%04x, length=%d", status, length);
        }
        
        /* Move to next packet (packets are 4-byte aligned) */
        /* Total packet size = 4 bytes (header) + length bytes (frame) */
        /* Round up to 4-byte boundary */
        rtl8139_rx_read_offset += ((4 + length + 3) & ~3);
        
        /* Wrap around if needed */
        if (rtl8139_rx_read_offset >= RTL8139_RX_BUF_SIZE)
            rtl8139_rx_read_offset -= RTL8139_RX_BUF_SIZE;
    }
    
    /* Update CAPR to acknowledge processed packets
     * CAPR indicates where we've read up to (minus 0x10 for hardware safety margin)
     */
    outw(rtl8139_io_base + RTL8139_REG_CAPR, (rtl8139_rx_read_offset - 0x10) & (RTL8139_RX_BUF_SIZE - 1));
    if (packet_count > 0)
    {
        /* Only log if we actually processed packets */
        LOG_INFO_FMT("RTL8139", "Processed %d packet(s)", packet_count);
    }
}

/**
 * rtl8139_handle_interrupt - Handle RTL8139 interrupt
 * 
 * This function processes received packets from the RTL8139 RX buffer.
 * It reads the interrupt status register, processes packets, and passes
 * them to the network stack via net_receive().
 */
void rtl8139_handle_interrupt(void)
{
    if (!rtl8139_io_base || !rtl8139_rx_buffer)
    {
        LOG_WARNING("RTL8139", "Interrupt handler called but device not initialized");
        return;
    }

    /* Read interrupt status register */
    uint16_t isr = inw(rtl8139_io_base + RTL8139_REG_ISR);
    
    /* Log ALL interrupts to serial (even if ISR=0) to verify handler is being called */
    /* Use serial directly to ensure we see it - this is critical for debugging */
    extern void serial_print(const char *);
    extern void serial_print_hex32(uint32_t);
    serial_print("[RTL8139] Interrupt handler called: ISR=0x");
    serial_print_hex32((uint32_t)isr);
    serial_print("\n");
    
    /* Check for receive interrupt */
    if (isr & RTL8139_INT_ROK)
    {
        LOG_DEBUG("RTL8139", "RX interrupt detected, processing packets...");
        rtl8139_process_rx_packets();
    }
    else if (isr != 0)
    {
        LOG_DEBUG("RTL8139", "Interrupt without RX (ISR does not have ROK bit set)");
    }
    
    /* Check for transmit interrupt (optional, for TX completion) */
    if (isr & RTL8139_INT_TOK)
    {
        LOG_DEBUG("RTL8139", "TX interrupt: transmission completed");
        /* CRITICAL: Check TX completion and update in-flight counter */
        rtl8139_check_tx_completion();
    }
    
    /* CRITICAL: Always check TX completion, even if no TX interrupt */
    /* Hardware might complete TX without generating interrupt */
    rtl8139_check_tx_completion();
    
    /* Clear interrupt status by writing back ISR */
    outw(rtl8139_io_base + RTL8139_REG_ISR, isr);
}

/**
 * rtl8139_poll - Poll for received packets (fallback when interrupts don't work)
 * 
 * This function can be called periodically to check for received packets
 * without relying on interrupts. Useful for debugging or when interrupts
 * are not working properly.
 */
void rtl8139_poll(void)
{
    if (!rtl8139_io_base || !rtl8139_rx_buffer)
    {
        return;
    }

    /* Check interrupt status register to see if there are packets */
    uint16_t isr = inw(rtl8139_io_base + RTL8139_REG_ISR);
    
    /* Debug: Log polling activity (only occasionally to avoid spam)
     * We log every 500 polls instead of 50 to reduce console spam.
     * If ISR shows activity or packets arrive, we'll see it in other logs.
     */
    static int poll_count = 0;
    poll_count++;
    if ((poll_count % 500) == 0)  /* Log every 500th poll to reduce spam */
    {
        extern void serial_print(const char *);
        extern void serial_print_hex32(uint32_t);
        extern char *itoa(int, char*, int);
        serial_print("[RTL8139] Poll #");
        char count_str[16];
        itoa(poll_count, count_str, 10);
        serial_print(count_str);
        serial_print(" ISR=0x");
        serial_print_hex32((uint32_t)isr);
        
        /* Also check write pointer */
        uint16_t current_write = *((volatile uint16_t *)(rtl8139_rx_buffer + 0x10));
        current_write &= (RTL8139_RX_BUF_SIZE - 1);
        serial_print(" write=");
        char write_str[16];
        itoa((int)current_write, write_str, 10);
        serial_print(write_str);
        serial_print(" read=");
        char read_str[16];
        itoa((int)rtl8139_rx_read_offset, read_str, 10);
        serial_print(read_str);
        serial_print("\n");
    }
    
    if (isr & RTL8139_INT_ROK)
    {
        /* Process packets */
        rtl8139_process_rx_packets();
        
        /* Clear interrupt status */
        outw(rtl8139_io_base + RTL8139_REG_ISR, RTL8139_INT_ROK);
    }
    else
    {
        /* Even if ISR doesn't show ROK, check the buffer directly */
        /* Sometimes packets arrive but interrupt doesn't fire */
        rtl8139_process_rx_packets();
    }
    
    /* CRITICAL: Check TX completion during polling */
    rtl8139_check_tx_completion();
}

