// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: dma.c
 * Description: DMA controller driver for 8-channel DMA with Sound Blaster integration
 */

#include "dma.h"
#include <arch/common/arch_interface.h>

// DMA controller ports
#define DMA1_COMMAND_REG    0x08
#define DMA1_STATUS_REG     0x08
#define DMA1_REQUEST_REG    0x09
#define DMA1_SINGLE_MASK    0x0A
#define DMA1_MODE_REG       0x0B
#define DMA1_CLEAR_FF       0x0C
#define DMA1_MASTER_CLEAR   0x0D
#define DMA1_CLEAR_MASK     0x0E
#define DMA1_ALL_MASK       0x0F

#define DMA2_COMMAND_REG    0xD0
#define DMA2_STATUS_REG     0xD0
#define DMA2_REQUEST_REG    0xD2
#define DMA2_SINGLE_MASK    0xD4
#define DMA2_MODE_REG       0xD6
#define DMA2_CLEAR_FF       0xD8
#define DMA2_MASTER_CLEAR   0xDA
#define DMA2_CLEAR_MASK     0xDC
#define DMA2_ALL_MASK       0xDE

// DMA channel ports
static const uint16_t dma_addr_ports[] = {0x00, 0x02, 0x04, 0x06, 0xC0, 0xC4, 0xC8, 0xCC};
static const uint16_t dma_count_ports[] = {0x01, 0x03, 0x05, 0x07, 0xC2, 0xC6, 0xCA, 0xCE};
static const uint16_t dma_page_ports[] = {0x87, 0x83, 0x81, 0x82, 0x8F, 0x8B, 0x89, 0x8A};

void dma_setup_channel(uint8_t channel, uint32_t addr, uint16_t length, bool is_16bit)
{
    if (channel > 7) return;
    
    // Disable the channel
    dma_disable_channel(channel);
    
    // Clear flip-flop
    if (channel < 4) {
        outb(DMA1_CLEAR_FF, 0);
    } else {
        outb(DMA2_CLEAR_FF, 0);
    }
    
    // Set mode (single transfer, read from memory)
    uint8_t mode = 0x48 | (channel & 3); // Single transfer, read
    if (channel < 4) {
        outb(DMA1_MODE_REG, mode);
    } else {
        outb(DMA2_MODE_REG, mode);
    }
    
    // Set address
    if (is_16bit && channel >= 4) {
        // 16-bit channels use word addresses
        addr >>= 1;
        length >>= 1;
    }
    
    outb(dma_addr_ports[channel], addr & 0xFF);
    outb(dma_addr_ports[channel], (addr >> 8) & 0xFF);
    
    // Set page
    outb(dma_page_ports[channel], (addr >> 16) & 0xFF);
    
    // Set count
    length--; // DMA uses length-1
    outb(dma_count_ports[channel], length & 0xFF);
    outb(dma_count_ports[channel], (length >> 8) & 0xFF);
}

void dma_enable_channel(uint8_t channel)
{
    if (channel > 7) return;
    
    if (channel < 4) {
        outb(DMA1_SINGLE_MASK, channel);
    } else {
        outb(DMA2_SINGLE_MASK, channel & 3);
    }
}

void dma_disable_channel(uint8_t channel)
{
    if (channel > 7) return;
    
    if (channel < 4) {
        outb(DMA1_SINGLE_MASK, 0x04 | channel);
    } else {
        outb(DMA2_SINGLE_MASK, 0x04 | (channel & 3));
    }
}