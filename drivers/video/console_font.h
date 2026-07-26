/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: console_font.h
 * Description: Early 8x16 boot font + product Terminus 14x28 glyphs.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/* Early / VGA fallback (always in kernel). */
#define FONT_EARLY_WIDTH  8
#define FONT_EARLY_HEIGHT 16

/* Product FB console font (native bitmap — not a scaled 8x16). */
#define FONT_PRODUCT_WIDTH  14
#define FONT_PRODUCT_HEIGHT 28
#define FONT_PRODUCT_CHARSIZE 56 /* ((14+7)/8)*28 */

/* Compat names: FB product path uses these as cell metrics. */
#define FONT_WIDTH  FONT_PRODUCT_WIDTH
#define FONT_HEIGHT FONT_PRODUCT_HEIGHT

extern const unsigned char font_8x16[256][FONT_EARLY_HEIGHT];
extern const unsigned char font_terminus_14x28[256][FONT_PRODUCT_CHARSIZE];
