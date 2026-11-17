/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: io.h
 * Description: x86 port I/O (IN/OUT). Only x86 code and PC platform drivers use it.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/* Implemented in arch/x86-64/sources/interface.c. */
uint8_t inb(uint16_t port);
void outb(uint16_t port, uint8_t value);
uint16_t inw(uint16_t port);
void outw(uint16_t port, uint16_t value);
uint32_t inl(uint16_t port);
void outl(uint16_t port, uint32_t value);

/* Short delay after a write to a slow ISA device (a write to port 0x80). */
void io_wait(void);
