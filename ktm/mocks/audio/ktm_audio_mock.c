/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_audio_mock.c
 * Description: KTM audio facade mock — records PCM requests, no SB16.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/audio_backend.h>
#include <ktm_mock_audio.h>

static int g_available;
static int g_play_result;
static int g_play_calls;
static size_t g_last_count;
static uint32_t g_last_sample_rate;
static uint8_t g_last_channels;
static uint8_t g_last_bits;
static uint8_t g_volume;
static int g_speaker_on_calls;
static int g_speaker_off_calls;
static int g_speaker_state;

void ktm_audio_mock_reset(void)
{
	g_available = 0;
	g_play_result = 0;
	g_play_calls = 0;
	g_last_count = 0;
	g_last_sample_rate = 0;
	g_last_channels = 0;
	g_last_bits = 0;
	g_volume = 0;
	g_speaker_on_calls = 0;
	g_speaker_off_calls = 0;
	g_speaker_state = 0;
}

void ktm_audio_mock_set_available(int on)
{
	g_available = on ? 1 : 0;
}

void ktm_audio_mock_set_play_result(int result)
{
	g_play_result = result;
}

void ktm_audio_mock_set_volume(uint8_t volume)
{
	g_volume = volume;
}

bool audio_backend_is_available(void)
{
	return g_available != 0;
}

int audio_backend_play_pcm(const void *buf, size_t count, uint32_t sample_rate,
			   uint8_t channels, uint8_t bits_per_sample)
{
	if (!buf || count == 0)
		return 0;

	g_play_calls++;
	g_last_count = count;
	g_last_sample_rate = sample_rate;
	g_last_channels = channels;
	g_last_bits = bits_per_sample;

	if (!g_available)
		return (int)count;
	if (g_play_result != 0)
		return -1;
	return (int)count;
}

void audio_backend_set_master_volume(uint8_t volume)
{
	g_volume = volume;
}

uint8_t audio_backend_get_master_volume(void)
{
	return g_volume;
}

void audio_backend_speaker_on(void)
{
	g_speaker_on_calls++;
	g_speaker_state = 1;
}

void audio_backend_speaker_off(void)
{
	g_speaker_off_calls++;
	g_speaker_state = 0;
}

int ktm_audio_mock_play_calls(void)
{
	return g_play_calls;
}

size_t ktm_audio_mock_last_count(void)
{
	return g_last_count;
}

uint32_t ktm_audio_mock_last_sample_rate(void)
{
	return g_last_sample_rate;
}

uint8_t ktm_audio_mock_last_channels(void)
{
	return g_last_channels;
}

uint8_t ktm_audio_mock_last_bits(void)
{
	return g_last_bits;
}

int ktm_audio_mock_speaker_on_calls(void)
{
	return g_speaker_on_calls;
}

int ktm_audio_mock_speaker_off_calls(void)
{
	return g_speaker_off_calls;
}

int ktm_audio_mock_speaker_state(void)
{
	return g_speaker_state;
}
