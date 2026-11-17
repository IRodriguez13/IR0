/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: audio_backend.h
 * Description: Audio playback facade for fs/devfs — no drivers/ includes.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A sound device registers its operations; the facade below forwards to it
 * and reports no device while none is registered.
 */
struct audio_ops
{
	bool (*is_available)(void);
	/* Play unsigned 8-bit mono PCM; 0 on success. */
	int (*play_pcm)(const void *buf, uint32_t size, uint32_t sample_rate);
	void (*set_master_volume)(uint8_t volume);
	uint8_t (*get_master_volume)(void);
	void (*speaker_on)(void);
	void (*speaker_off)(void);
	/* Exercise the device's DMA/IRQ path once interrupts are enabled. */
	void (*post_irq_selftest)(void);
};

void audio_register_device(const struct audio_ops *ops);

bool audio_backend_is_available(void);
int audio_backend_play_pcm(const void *buf, size_t count, uint32_t sample_rate,
			   uint8_t channels, uint8_t bits_per_sample);
void audio_backend_set_master_volume(uint8_t volume);
uint8_t audio_backend_get_master_volume(void);
void audio_backend_speaker_on(void);
void audio_backend_speaker_off(void);
/* Boot calls this once interrupts are on; no-op without an available device. */
void audio_backend_post_irq_selftest(void);
