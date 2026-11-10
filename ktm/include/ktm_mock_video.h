/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_mock_video.h
 * Description: Test-only video/framebuffer facade mock control surface.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

void ktm_video_mock_reset(void);

void ktm_video_mock_set_available(int on);
void ktm_video_mock_set_init_results(int boot_result, int fallback_result);
void ktm_video_mock_set_info(uint32_t width, uint32_t height, uint32_t bpp);
void ktm_video_mock_set_pitch(uint32_t pitch);
void ktm_video_mock_set_fb_phys(uint64_t phys);
void ktm_video_mock_set_fb_size(uint64_t size);
void ktm_video_mock_set_fail_reason(int reason);

uint8_t *ktm_video_mock_framebuffer(void);
int ktm_video_mock_boot_calls(void);
int ktm_video_mock_fallback_calls(void);
