/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_mock_audio.h
 * Description: Test-only audio facade mock control surface.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

void ktm_audio_mock_reset(void);

void ktm_audio_mock_set_available(int on);
void ktm_audio_mock_set_play_result(int result);
void ktm_audio_mock_set_volume(uint8_t volume);

int ktm_audio_mock_play_calls(void);
size_t ktm_audio_mock_last_count(void);
uint32_t ktm_audio_mock_last_sample_rate(void);
uint8_t ktm_audio_mock_last_channels(void);
uint8_t ktm_audio_mock_last_bits(void);
int ktm_audio_mock_speaker_on_calls(void);
int ktm_audio_mock_speaker_off_calls(void);
int ktm_audio_mock_speaker_state(void);
