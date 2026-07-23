/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arm64_board.h
 * Description: Compile-time ARM64 board descriptor (UART MMIO, platform_ops).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/platform_ops.h>
#include <stdint.h>

struct arm64_board_desc
{
	const char *name;	 /* qemu-virt | rpi4 | rpi5 */
	const char *uart;	 /* pl011 | none */
	uintptr_t uart_mmio; /* 0 = no UART (honest stub) */
	const char *arch_line;	  /* for ir0_boot_arch() */
	const char *uart_mmio_line; /* optional second ARCH line; may be NULL */
	const struct ir0_platform_ops *platform_ops;
};

extern const struct ir0_platform_ops arm64_virt_platform_ops;
extern const struct ir0_platform_ops arm64_rpi_platform_ops;

const struct arm64_board_desc *arm64_board_get(void);

/* Select platform_ops from the active board (call once early). */
void arm64_board_apply_platform(void);

/* Emit ARCH lines (and WARN if uart_mmio == 0). */
void arm64_board_log_arch(void);
