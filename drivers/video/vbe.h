/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vbe.h
 * Description: IR0 kernel source/header file
 */

// VBE (VESA BIOS Extensions) Framebuffer Driver Header
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Diagnostic: 0=ok, 1=mb_null, 2=no_fb_flag, 3=bad_dims, 4=map_page_failed */
extern int vbe_fail_reason;

/* Initialize from Multiboot info (call early, pass ebx). Returns 0 on success. */
int vbe_init_from_multiboot(uint32_t multiboot_info);

/* Fallback init (VGA text mode if no multiboot fb) */
int vbe_init(void);

// Clear screen with color
void vbe_clear(uint32_t color);

// Put pixel at coordinates (for graphics mode)
void vbe_putpixel(uint32_t x, uint32_t y, uint32_t color);

// Put character at coordinates
void vbe_putchar(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);

// Print string at coordinates
void vbe_print_at(uint32_t x, uint32_t y, const char *str, uint32_t fg, uint32_t bg);

/* Get framebuffer information */
bool vbe_get_info(uint32_t *width, uint32_t *height, uint32_t *bpp);

/* Get pitch (bytes per line) and raw framebuffer pointer */
uint32_t vbe_get_pitch(void);
uint8_t *vbe_get_fb(void);

/* For mmap: physical address and size (page-aligned) */
uint32_t vbe_get_fb_phys(void);
uint32_t vbe_get_fb_size(void);

// Check if VBE is available
bool vbe_is_available(void);

/*
 * vbe_rgb_to_pixel - Convert R,G,B (0-255) to framebuffer pixel format.
 * Uses multiboot color_info for correct BGR/RGB layout.
 */
uint32_t vbe_rgb_to_pixel(uint8_t r, uint8_t g, uint8_t b);

// Color macros (RGB)
#define VBE_RGB(r, g, b) (((r) << 16) | ((g) << 8) | (b))
#define VBE_BLACK   0x000000
#define VBE_WHITE   0xFFFFFF
#define VBE_RED     0xFF0000
#define VBE_GREEN   0x00FF00
#define VBE_BLUE    0x0000FF
#define VBE_CYAN    0x00FFFF
#define VBE_YELLOW  0xFFFF00
#define VBE_MAGENTA 0xFF00FF
