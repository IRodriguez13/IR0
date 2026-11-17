/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: audio_backend.c
 * Description: Audio facade — forwards to the registered sound device.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/audio_backend.h>

/* The sound device the platform registered; NULL when it has none. */
static const struct audio_ops *audio;

void audio_register_device(const struct audio_ops *ops)
{
	audio = ops;
}

bool audio_backend_is_available(void)
{
	return audio && audio->is_available();
}

int audio_backend_play_pcm(const void *buf, size_t count, uint32_t sample_rate,
			   uint8_t channels, uint8_t bits_per_sample)
{
	int ret;

	if (!audio)
		return -1;
	if (!buf || count == 0)
		return 0;
	if (!audio->is_available())
		return (int)count;
	(void)channels;
	(void)bits_per_sample;
	ret = audio->play_pcm(buf, (uint32_t)count, sample_rate);
	return (ret == 0) ? (int)count : -1;
}

void audio_backend_set_master_volume(uint8_t volume)
{
	if (audio)
		audio->set_master_volume(volume);
}

uint8_t audio_backend_get_master_volume(void)
{
	return audio ? audio->get_master_volume() : 0;
}

void audio_backend_speaker_on(void)
{
	if (audio)
		audio->speaker_on();
}

void audio_backend_speaker_off(void)
{
	if (audio)
		audio->speaker_off();
}

void audio_backend_post_irq_selftest(void)
{
	if (audio && audio->is_available())
		audio->post_irq_selftest();
}
