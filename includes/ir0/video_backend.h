/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: video_backend.h
 * Description: Framebuffer facade — no drivers/ includes (impl in kernel/video_backend.c).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdbool.h>
#include <stdint.h>

int video_backend_init_from_multiboot(uint32_t multiboot_info);
int video_backend_init_fallback(void);
bool video_backend_is_available(void);
int video_backend_fail_reason(void);
bool video_backend_get_info(uint32_t *width, uint32_t *height, uint32_t *bpp);
uint8_t *video_backend_get_fb(void);
uint32_t video_backend_get_pitch(void);
uint32_t video_backend_get_fb_phys(void);
uint32_t video_backend_get_fb_size(void);
