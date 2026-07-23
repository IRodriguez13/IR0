/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: io.h
 * Description: Port I/O helpers — polymorphic facades (no inline ISA asm).
 */

#pragma once

#include <stdint.h>
#include <ir0/cpu.h>
#include <ir0/arch_io.h>

static inline void io_wait(void)
{
	outb(0x80, 0);
}
