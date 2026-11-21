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
#include <asm/io.h>

#include <stdint.h>

static void vga_text_cursor(int row, int col, int on);

/* PC VGA text mode: 80x25 cells at the legacy text frame buffer. */
static const struct console_text_screen pc_vga_text = {
	.cells = (volatile uint16_t *)0xB8000,
	.cols = 80,
	.rows = 25,
	.cursor = vga_text_cursor,
};

/* CRTC cursor location is 0x0E/0x0F; 0x0A bit 5 hides it. */
static void vga_text_cursor(int row, int col, int on)
{
	unsigned int pos;
	uint8_t start;

	if (row < 0 || col < 0 || row >= pc_vga_text.rows ||
	    col >= pc_vga_text.cols)
		return;

	pos = (unsigned int)row * (unsigned int)pc_vga_text.cols +
	      (unsigned int)col;
	outb(0x3D4, 0x0F);
	outb(0x3D5, (uint8_t)pos);
	outb(0x3D4, 0x0E);
	outb(0x3D5, (uint8_t)(pos >> 8));

	outb(0x3D4, 0x0A);
	start = inb(0x3D5);
	if (on)
		start = (uint8_t)(start & (uint8_t)~0x20);
	else
		start = (uint8_t)(start | 0x20);
	outb(0x3D4, 0x0A);
	outb(0x3D5, start);
}

void ir0_boot_arch_firmware_early(const struct ir0_boot_params *params)
{
	(void)params;
	console_set_text_screen(&pc_vga_text);
}

void ir0_boot_arch_userspace_handoff_smoke(void)
{
}
