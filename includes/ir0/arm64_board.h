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
	int (*boot_info_init)(uintptr_t fdt_pa);
};

struct arm64_board_boot_info
{
	uintptr_t fdt_pa;
	uint32_t fdt_size;
	int fdt_valid;
};

extern const struct ir0_platform_ops arm64_virt_platform_ops;
extern const struct ir0_platform_ops arm64_rpi_platform_ops;

const struct arm64_board_desc *arm64_board_get(void);

/* Select platform_ops from the active board (call once early). */
void arm64_board_apply_platform(void);

/* Capture firmware x0 through the board-selected boot information backend. */
int arm64_board_capture_boot_info(uintptr_t fdt_pa);
const struct arm64_board_boot_info *arm64_board_boot_info(void);

/* Shared ARM64 FDT backend used by DT-described boards. */
int arm64_fdt_boot_info_init(uintptr_t fdt_pa);

/* Emit ARCH lines (and WARN if uart_mmio == 0). */
void arm64_board_log_arch(void);
