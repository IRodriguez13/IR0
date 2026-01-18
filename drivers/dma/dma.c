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
 *
 * Direct Memory Access (DMA) allows hardware devices to transfer data directly
 * to/from memory without CPU intervention, which is critical for high-speed
 * peripherals like sound cards and network adapters. The 8237 controller provides
 * 8 DMA channels: channels 0-3 are 8-bit (can address up to 64KB), channels 4-7
 * are 16-bit (can address up to 128KB). Channels 0-3 are managed by DMA controller
 * 1, channels 4-7 by DMA controller 2 (which is cascaded through channel 4).
 *
 * Physical addresses in the 8237 are split into three parts:
 * - Address register (low 16 bits, ports 0x00-0x06 for channels 0-3)
 * - Page register (bits 16-23, ports 0x87-0x82 for channels 0-3)
 * - Count register (transfer length - 1, ports 0x01-0x07)
 *
 * When programming DMA, you must disable the channel first, clear the flip-flop
 * to reset byte ordering, configure the mode, set address/count, then enable.
 */

#include "dma.h"
#include <arch/common/arch_interface.h>

/*
 * Port mappings for the 8237 DMA controllers. These arrays map channel numbers
 * to their respective I/O ports for address, count, and page registers.
 *
 * Channels 0-3 (8-bit, DMA1): Use ports 0x00-0x0F for most operations
 * Channels 4-7 (16-bit, DMA2): Use ports 0xC0-0xDE, cascaded through channel 4
 */
static const uint16_t dma_addr_ports[] = { 0x00, 0x02, 0x04, 0x06, 0xC0, 0xC4, 0xC8, 0xCC };
static const uint16_t dma_count_ports[] = { 0x01, 0x03, 0x05, 0x07, 0xC2, 0xC6, 0xCA, 0xCE };
static const uint16_t dma_page_ports[] = { 0x87, 0x83, 0x81, 0x82, 0x8F, 0x8B, 0x89, 0x8A };

/**
 * dma_setup_channel - Configure a DMA channel for data transfer
 *
 * This function programs the 8237 DMA controller to transfer data between
 * a physical memory buffer and a hardware device. The setup sequence is:
 *
 *   1. Disable the channel (prevents interference during configuration)
 *   2. Clear the flip-flop (resets byte ordering for 16-bit writes)
 *   3. Set transfer mode (single transfer, direction, channel selection)
 *   4. Program address and count registers (split into low/high bytes)
 *   5. Set page register (bits 16-23 of physical address)
 *
 * For 16-bit channels (4-7), addresses and lengths are converted to word
 * addresses (divided by 2), and the buffer must be 16-bit aligned.
 *
 * Note: The transfer count is stored as (length - 1) because the DMA controller
 * transfers length bytes when count is programmed as length-1. This matches
 * the hardware behavior where 0 means 1 byte, 1 means 2 bytes, etc.
 *
 * @channel: DMA channel number (0-7, where 0-3 are 8-bit, 4-7 are 16-bit)
 * @addr: Physical address of the transfer buffer (must be aligned for 16-bit)
 * @length: Transfer size in bytes (must be even for 16-bit channels)
 * @is_16bit: True if channel is 16-bit (channels 4-7), false for 8-bit (0-3)
 */
void dma_setup_channel(uint8_t channel, uint32_t addr, uint16_t length, bool is_16bit)
{
    uint8_t mode;

    if (channel > 7)
    {
        return;
    }
    
    /* Disable the channel before configuration to prevent race conditions.
     * If we don't do this, the DMA controller might start a transfer with
     * partially configured parameters.
     */
    dma_disable_channel(channel);
    
    /* Clear the flip-flop register. The 8237 uses an internal flip-flop
     * to track whether the next write to address/count registers is the
     * low or high byte. Clearing it ensures we start with the low byte.
     * This is critical because address/count are written as two consecutive
     * 8-bit writes, and the flip-flop tracks the order.
     */
    if (channel < 4) 
    {
        outb(DMA1_CLEAR_FF, 0);
    } 
    else 
    {
        outb(DMA2_CLEAR_FF, 0);
    }
    
    /* Configure transfer mode. DMA_SB_MODE_READ means "read from memory"
     * (device is the destination). For Sound Blaster compatibility, we use
     * single transfer mode (one byte/word at a time) rather than block mode.
     * The channel number is encoded in the lower 2 bits of the mode register.
     */
    mode = DMA_SB_MODE_READ | (channel & 3);
    if (channel < 4) 
    {
        outb(DMA1_MODE_REG, mode);
    } 
    else 
    {
        outb(DMA2_MODE_REG, mode);
    }
    
    /* For 16-bit channels, the address and count are in word units rather
     * than byte units. This is a quirk of the 8237 - it was designed for
     * 8-bit systems and later extended. The physical address must also be
     * 16-bit aligned for these channels.
     */
    if (is_16bit && channel >= 4) 
    {
        addr >>= 1;   /* Convert byte address to word address */
        length >>= 1; /* Convert byte count to word count */
    }
    
    /* Write address register (low 16 bits). The flip-flop we cleared earlier
     * ensures the first write goes to the low byte, second to high byte.
     */
    outb(dma_addr_ports[channel], addr & 0xFF);         /* Low byte */
    outb(dma_addr_ports[channel], (addr >> 8) & 0xFF);  /* High byte */
    
    /* Set page register (bits 16-23 of physical address). The 8237 address
     * register only holds 16 bits, so the page register extends addressing
     * to 24 bits (16MB). This is why DMA buffers must be below 16MB physical.
     */
    outb(dma_page_ports[channel], (addr >> 16) & 0xFF);
    
    /* Write count register (transfer length - 1). The DMA controller transfers
     * (count + 1) bytes/words, so we subtract 1 from the desired length.
     * Again, the flip-flop ensures low byte first, then high byte.
     */
    length--;
    outb(dma_count_ports[channel], length & 0xFF);         /* Low byte */
    outb(dma_count_ports[channel], (length >> 8) & 0xFF);  /* High byte */
}

/**
 * dma_enable_channel - Enable a DMA channel to start transfers
 *
 * After configuring a channel with dma_setup_channel(), this function enables
 * the channel to begin DMA transfers. The single mask register is used to
 * clear the mask bit for the specified channel, allowing the device to request
 * DMA transfers. For channels 4-7, we mask with 3 because DMA2 uses internal
 * channel numbers 0-3 (it's cascaded through channel 4 of DMA1).
 *
 * Once enabled, the channel will respond to DMA requests from the hardware
 * device. The transfer will proceed according to the mode configured in
 * dma_setup_channel() (single transfer, block transfer, etc.).
 *
 * @channel: DMA channel number (0-7)
 */
void dma_enable_channel(uint8_t channel)
{
    if (channel > 7)
    {
        return;
    }
    
    /* Writing channel number to single mask register clears the mask bit,
     * enabling the channel. For DMA2 (channels 4-7), we use (channel & 3)
     * because DMA2 internally numbers its channels 0-3.
     */
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
 * dma_disable_channel - Disable a DMA channel to stop transfers
 *
 * This function masks the specified DMA channel, preventing it from accepting
 * DMA requests. This is useful when reconfiguring a channel or when a transfer
 * completes. Setting bit 2 (0x04) in the mask register sets the mask bit,
 * which disables the channel regardless of what the channel number bits are.
 *
 * Disabling a channel that's actively transferring may leave the channel in
 * an undefined state, so it's generally safer to wait for transfers to complete
 * before disabling. However, for reconfiguration, disabling first ensures clean
 * state transitions.
 *
 * @channel: DMA channel number (0-7)
 */
void dma_disable_channel(uint8_t channel)
{
    uint8_t mask_val = 0x04; /* Bit 2: set mask (disable channel) */

    if (channel > 7)
    {
        return;
    }
    
    /* Writing (mask_bit | channel_number) sets the mask, disabling the channel.
     * This prevents the hardware device from initiating DMA transfers until
     * the channel is re-enabled with dma_enable_channel().
     */
    if (channel < 4) 
    {
        outb(DMA1_SINGLE_MASK, mask_val | channel);
    } 
    else 
    {
        outb(DMA2_SINGLE_MASK, mask_val | (channel & 3));
    }
}