/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Video backend API
 *
 * Driver-agnostic framebuffer entrypoints used by core/kernel layers.
 */

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
