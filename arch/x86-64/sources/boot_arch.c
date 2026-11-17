/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_arch.c
 * Description: x86-64 boot_arch hooks — PC platform setup before the console.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/boot_arch.h>
#include <ir0/video_console.h>

#include <stdint.h>

/* PC VGA text mode: 80x25 cells at the legacy text frame buffer. */
static const struct console_text_screen pc_vga_text = {
	.cells = (volatile uint16_t *)0xB8000,
	.cols = 80,
	.rows = 25,
};

void ir0_boot_arch_firmware_early(const struct ir0_boot_params *params)
{
	(void)params;
	console_set_text_screen(&pc_vga_text);
}
