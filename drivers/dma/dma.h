/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: dma.h
 * Description: DMA controller driver header
 */

#ifndef DMA_H
#define DMA_H

#include <stdint.h>
#include <stdbool.h>

/* DMA Controller 1 (8-bit) Ports */
#define DMA1_COMMAND_REG    0x08
#define DMA1_STATUS_REG     0x08
#define DMA1_REQUEST_REG    0x09
#define DMA1_SINGLE_MASK    0x0A
#define DMA1_MODE_REG       0x0B
#define DMA1_CLEAR_FF       0x0C
#define DMA1_MASTER_CLEAR   0x0D
#define DMA1_CLEAR_MASK     0x0E
#define DMA1_ALL_MASK       0x0F

/* DMA Controller 2 (16-bit) Ports */
#define DMA2_COMMAND_REG    0xD0
#define DMA2_STATUS_REG     0xD0
#define DMA2_REQUEST_REG    0xD2
#define DMA2_SINGLE_MASK    0xD4
#define DMA2_MODE_REG       0xD6
#define DMA2_CLEAR_FF       0xD8
#define DMA2_MASTER_CLEAR   0xDA
#define DMA2_CLEAR_MASK     0xDC
#define DMA2_ALL_MASK       0xDE

/* DMA Mode bits */
#define DMA_MODE_SEL_MASK   0x03
#define DMA_MODE_TRA_MASK   0x0C
#define DMA_MODE_SELF_TEST  0x00
#define DMA_MODE_READ       0x04
#define DMA_MODE_WRITE      0x08
#define DMA_MODE_AUTO       0x10
#define DMA_MODE_DOWN       0x20
#define DMA_MODE_SINGLE     0x40
#define DMA_MODE_BLOCK      0x80
#define DMA_MODE_CASCADE    0xC0

/* DMA configuration for Sound Blaster */
#define DMA_SB_MODE_READ    (DMA_MODE_SINGLE | DMA_MODE_READ)

/* DMA functions */
void dma_setup_channel(uint8_t channel, uint32_t addr, uint16_t length, bool is_16bit);
void dma_enable_channel(uint8_t channel);
void dma_disable_channel(uint8_t channel);

#endif /* DMA_H */