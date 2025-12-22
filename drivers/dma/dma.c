/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: dma.c
 * Description: Intel 8237 DMA controller driver (8 and 16-bit channels)
 */

#include "dma.h"
#include <arch/common/arch_interface.h>

/* DMA channel ports */
static const uint16_t dma_addr_ports[] = { 0x00, 0x02, 0x04, 0x06, 0xC0, 0xC4, 0xC8, 0xCC };
static const uint16_t dma_count_ports[] = { 0x01, 0x03, 0x05, 0x07, 0xC2, 0xC6, 0xCA, 0xCE };
static const uint16_t dma_page_ports[] = { 0x87, 0x83, 0x81, 0x82, 0x8F, 0x8B, 0x89, 0x8A };

/**
 * dma_setup_channel - configure a DMA channel for transfer
 * @channel: channel number (0-7)
 * @addr: physical address of the buffer
 * @length: transfer size in bytes
 * @is_16bit: true if using a 16-bit channel (4-7)
 */
void dma_setup_channel(uint8_t channel, uint32_t addr, uint16_t length, bool is_16bit)
{
    uint8_t mode;

    if (channel > 7)
    {
        return;
    }
    
    /* Disable the channel before configuration */
    dma_disable_channel(channel);
    
    /* Clear flip-flop to reset high/low byte pointer */
    if (channel < 4) 
    {
        outb(DMA1_CLEAR_FF, 0);
    } 
    else 
    {
        outb(DMA2_CLEAR_FF, 0);
    }
    
    /* Set mode (single transfer, read from memory) */
    mode = DMA_SB_MODE_READ | (channel & 3);
    if (channel < 4) 
    {
        outb(DMA1_MODE_REG, mode);
    } 
    else 
    {
        outb(DMA2_MODE_REG, mode);
    }
    
    /* Set physical address */
    if (is_16bit && channel >= 4) 
    {
        /* 16-bit channels use word addresses (must be 16-bit aligned) */
        addr >>= 1;
        length >>= 1;
    }
    
    outb(dma_addr_ports[channel], addr & 0xFF);         /* Low byte */
    outb(dma_addr_ports[channel], (addr >> 8) & 0xFF);  /* High byte */
    
    /* Set page register (bits 16-23) */
    outb(dma_page_ports[channel], (addr >> 16) & 0xFF);
    
    /* Set transfer count (DMA uses length - 1) */
    length--;
    outb(dma_count_ports[channel], length & 0xFF);         /* Low byte */
    outb(dma_count_ports[channel], (length >> 8) & 0xFF);  /* High byte */
}

/**
 * dma_enable_channel - enable a DMA channel for transfer
 * @channel: channel number (0-7)
 */
void dma_enable_channel(uint8_t channel)
{
    if (channel > 7)
    {
        return;
    }
    
    if (channel < 4) 
    {
        outb(DMA1_SINGLE_MASK, channel);
    } 
    else 
    {
        outb(DMA2_SINGLE_MASK, channel & 3);
    }
}

/**
 * dma_disable_channel - disable a DMA channel
 * @channel: channel number (0-7)
 */
void dma_disable_channel(uint8_t channel)
{
    uint8_t mask_val = 0x04; /* Mask bit */

    if (channel > 7)
    {
        return;
    }
    
    if (channel < 4) 
    {
        outb(DMA1_SINGLE_MASK, mask_val | channel);
    } 
    else 
    {
        outb(DMA2_SINGLE_MASK, mask_val | (channel & 3));
    }
}