/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - 8x16 VGA font for framebuffer console
 * Minimal subset for ASCII 0x20-0x7F (96 chars).
 * Format: 16 bytes per char, each byte = one row (bit 7 = left pixel).
 */
#ifndef CONSOLE_FONT_H
#define CONSOLE_FONT_H

#define FONT_WIDTH  8
#define FONT_HEIGHT 16

extern const unsigned char font_8x16[256][16];

#endif /* CONSOLE_FONT_H */
