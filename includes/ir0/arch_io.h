/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_io.h
 * Description: Portable port-I/O and MMIO accessors.
 */

#pragma once

#include <stdint.h>
#include <ir0/arch_types.h>

uint8_t inb(uint16_t port);
void outb(uint16_t port, uint8_t value);

uint8_t mmio_read8(arch_addr_t addr);
void mmio_write8(arch_addr_t addr, uint8_t value);
uint16_t mmio_read16(arch_addr_t addr);
void mmio_write16(arch_addr_t addr, uint16_t value);
uint32_t mmio_read32(arch_addr_t addr);
void mmio_write32(arch_addr_t addr, uint32_t value);

/* Short platform I/O delay; a no-op on architectures without port I/O. */
void io_wait(void);

/* Wider port I/O; a no-op returning zero on ISAs without port I/O. */
uint16_t inw(uint16_t port);
void outw(uint16_t port, uint16_t value);
uint32_t inl(uint16_t port);
void outl(uint16_t port, uint32_t value);

uintptr_t read_fault_address(void);

void cpu_wait(void);
