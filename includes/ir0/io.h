/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: io.h
 * Description: Kernel mappings of device memory (Linux ioremap).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * ioremap - Kernel pointer to the device registers at @phys.
 * @phys: physical address of the registers.
 * @size: bytes the driver will touch from @phys.
 *
 * The mapping is uncached device memory, the same in every address space,
 * and lasts for the kernel's lifetime. Returns NULL when it cannot be made.
 */
void *ioremap(uintptr_t phys, size_t size);

/*
 * ioremap_wc - Kernel pointer to a framebuffer at @phys, write-combining.
 * Same contract as ioremap() for memory the CPU writes and a display reads.
 */
void *ioremap_wc(uintptr_t phys, size_t size);
