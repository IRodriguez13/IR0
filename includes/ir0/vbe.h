/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vbe.h
 * Description: VBE framebuffer driver facade
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdbool.h>
#include <stdint.h>

extern int vbe_fail_reason;

int vbe_init_from_multiboot(uint32_t multiboot_info);
int vbe_init(void);
void vbe_clear(uint32_t color);
void vbe_putpixel(uint32_t x, uint32_t y, uint32_t color);
void vbe_putchar(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void vbe_print_at(uint32_t x, uint32_t y, const char *str, uint32_t fg,
		  uint32_t bg);
bool vbe_get_info(uint32_t *width, uint32_t *height, uint32_t *bpp);
uint32_t vbe_get_pitch(void);
uint8_t *vbe_get_fb(void);
uint32_t vbe_get_fb_phys(void);
uint32_t vbe_get_fb_size(void);
bool vbe_is_available(void);
uint32_t vbe_rgb_to_pixel(uint8_t r, uint8_t g, uint8_t b);

#define VBE_RGB(r, g, b) (((r) << 16) | ((g) << 8) | (b))
#define VBE_BLACK   0x000000
#define VBE_WHITE   0xFFFFFF
#define VBE_RED     0xFF0000
#define VBE_GREEN   0x00FF00
#define VBE_BLUE    0x0000FF
#define VBE_CYAN    0x00FFFF
#define VBE_YELLOW  0xFFFF00
#define VBE_MAGENTA 0xFF00FF
