/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_video_mock.c
 * Description: KTM framebuffer facade mock — synthetic mode info, no VBE.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/video_backend.h>
#include <ktm_mock_video.h>

#define KTM_VIDEO_MOCK_FB_SIZE 4096u

static uint8_t g_fb[KTM_VIDEO_MOCK_FB_SIZE];
static int g_available;
static int g_multiboot_result;
static int g_fallback_result;
static int g_multiboot_calls;
static int g_fallback_calls;
static int g_fail_reason;
static uint32_t g_width = 640;
static uint32_t g_height = 480;
static uint32_t g_bpp = 32;
static uint32_t g_pitch = 640u * 4u;
static uint32_t g_fb_phys = 0xFD000000u;
static uint32_t g_fb_size = KTM_VIDEO_MOCK_FB_SIZE;

void ktm_video_mock_reset(void)
{
	unsigned i;

	for (i = 0; i < KTM_VIDEO_MOCK_FB_SIZE; i++)
		g_fb[i] = 0;
	g_available = 0;
	g_multiboot_result = 0;
	g_fallback_result = 0;
	g_multiboot_calls = 0;
	g_fallback_calls = 0;
	g_fail_reason = 0;
	g_width = 640;
	g_height = 480;
	g_bpp = 32;
	g_pitch = 640u * 4u;
	g_fb_phys = 0xFD000000u;
	g_fb_size = KTM_VIDEO_MOCK_FB_SIZE;
}

void ktm_video_mock_set_available(int on)
{
	g_available = on ? 1 : 0;
}

void ktm_video_mock_set_init_results(int multiboot_result, int fallback_result)
{
	g_multiboot_result = multiboot_result;
	g_fallback_result = fallback_result;
}

void ktm_video_mock_set_info(uint32_t width, uint32_t height, uint32_t bpp)
{
	g_width = width;
	g_height = height;
	g_bpp = bpp;
}

void ktm_video_mock_set_pitch(uint32_t pitch)
{
	g_pitch = pitch;
}

void ktm_video_mock_set_fb_phys(uint32_t phys)
{
	g_fb_phys = phys;
}

void ktm_video_mock_set_fb_size(uint32_t size)
{
	g_fb_size = size;
}

void ktm_video_mock_set_fail_reason(int reason)
{
	g_fail_reason = reason;
}

int video_backend_init_from_multiboot(uint32_t multiboot_info)
{
	(void)multiboot_info;
	g_multiboot_calls++;
	if (g_multiboot_result == 0)
		g_available = 1;
	return g_multiboot_result;
}

int video_backend_init_fallback(void)
{
	g_fallback_calls++;
	if (g_fallback_result == 0)
		g_available = 1;
	return g_fallback_result;
}

bool video_backend_is_available(void)
{
	return g_available != 0;
}

int video_backend_fail_reason(void)
{
	return g_fail_reason;
}

bool video_backend_get_info(uint32_t *width, uint32_t *height, uint32_t *bpp)
{
	if (!g_available || !width || !height || !bpp)
		return false;
	*width = g_width;
	*height = g_height;
	*bpp = g_bpp;
	return true;
}

uint8_t *video_backend_get_fb(void)
{
	return g_available ? g_fb : (uint8_t *)0;
}

uint32_t video_backend_get_pitch(void)
{
	return g_pitch;
}

uint32_t video_backend_get_fb_phys(void)
{
	return g_fb_phys;
}

uint32_t video_backend_get_fb_size(void)
{
	return g_fb_size;
}

uint8_t *ktm_video_mock_framebuffer(void)
{
	return g_fb;
}

int ktm_video_mock_multiboot_calls(void)
{
	return g_multiboot_calls;
}

int ktm_video_mock_fallback_calls(void)
{
	return g_fallback_calls;
}
