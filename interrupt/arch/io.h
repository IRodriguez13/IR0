/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: io.h
 * Description: IR0 kernel source/header file
 */

#pragma once

#include <stdint.h>
#include <arch/common/arch_interface.h>

// Funciones de I/O
// Using I/O functions from arch_interface.h

static inline uint16_t inw(uint16_t port) 
{
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) 
{
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) 
{
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val) 
{
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

// Funciones de espera
static inline void io_wait(void) 
{
    outb(0x80, 0);
}

