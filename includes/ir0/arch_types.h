/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_types.h
 * Description: Portable address/IRQ typedefs shared by ISA facades.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

typedef uintptr_t arch_addr_t;
typedef uintptr_t arch_size_t;
typedef uint32_t arch_irq_t;
typedef uint32_t arch_flags_t;
