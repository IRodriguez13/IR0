/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: gic_v2.h
 * Description: QEMU virt GICv2 Dist/CPU iface for freestanding timer IRQ.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/** EL1 physical timer PPI (ARM Generic Timer). */
#define ARM64_GIC_PPI_PHYS_TIMER 30U

/** Enable Dist+CPU, PMR, and PPI @irq. Returns 0 on success. */
int arm64_gic_v2_enable(uint32_t irq);

/** Write GICC_EOIR after handling @irq. */
void arm64_gic_v2_eoi(uint32_t irq);

/** Read GICC_IAR (acknowledge). */
uint32_t arm64_gic_v2_ack(void);
