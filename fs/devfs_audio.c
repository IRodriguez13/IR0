/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: devfs_audio.c
 * Description: PCM audio device node
 */

#include "devfs_internal.h"

#if CONFIG_ENABLE_SOUND
/*
 * PCM format state for /dev/audio. Default: Doom-compatible 11025 Hz, 8-bit mono.
 * Use ioctl(AUDIO_SET_FORMAT) before write() to change.
 */
static struct audio_format audio_pcm_format = {
    .sample_rate = 11025,
    .channels = 1,
    .bits_per_sample = 8
};
#endif

int64_t dev_audio_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry;
    (void)offset;
#if CONFIG_ENABLE_SOUND
    if (!audio_backend_is_available())
    {
        /* Sound Blaster not available, accept data but don't process */
        return (int64_t)count;
    }
    if (!buf || count == 0)
        return (int64_t)count;

    return (int64_t)audio_backend_play_pcm(buf, count,
                                           audio_pcm_format.sample_rate,
                                           audio_pcm_format.channels,
                                           audio_pcm_format.bits_per_sample);
#else
    (void)buf; (void)count;
    return -ENODEV;
#endif
}

int64_t dev_audio_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    (void)entry;
#if CONFIG_ENABLE_SOUND
    if (!audio_backend_is_available())
    {
        return -1;  /* Device not available */
    }
    
    switch (request)
    {
        case AUDIO_SET_VOLUME:
            if (arg)
            {
                uint8_t volume;
                if (copy_from_user(&volume, arg, sizeof(volume)) != 0)
                    return -EFAULT;
                if (volume > 100)
                    volume = 100;  /* Clamp to 0-100 */
                /* Convert 0-100 to 0x00-0xFF mixer value */
                uint8_t mixer_vol = (volume * 255) / 100;
                audio_backend_set_master_volume(mixer_vol);
                return 0;
            }
            return -1;
            
        case AUDIO_GET_VOLUME:
            if (arg)
            {
                uint8_t mixer_vol = audio_backend_get_master_volume();
                /* Mixer value format: bits 7-4 = left, bits 3-0 = right */
                uint8_t left = (mixer_vol >> 4) & 0x0F;
                uint8_t right = mixer_vol & 0x0F;
                uint8_t avg = (left + right) / 2;
                uint8_t volume = (avg * 100) / 15;
                if (copy_to_user(arg, &volume, sizeof(volume)) != 0)
                    return -EFAULT;
                return 0;
            }
            return -1;
            
        case AUDIO_PLAY:
            audio_backend_speaker_on();
            return 0;
            
        case AUDIO_STOP:
            audio_backend_speaker_off();
            return 0;
            
        case AUDIO_SET_FORMAT:
            if (arg)
            {
                struct audio_format fmt;
                if (copy_from_user(&fmt, arg, sizeof(fmt)) != 0)
                    return -1;
                /* SB16 8-bit mono: 4000-45454 Hz. Doom uses 11025. */
                if (fmt.sample_rate < 4000 || fmt.sample_rate > 45454)
                    return -1;
                if (fmt.channels != 1 || fmt.bits_per_sample != 8)
                    return -1;  /* 16-bit/stereo not yet implemented */
                audio_pcm_format = fmt;
                return 0;
            }
            return -1;
            
        case AUDIO_GET_FORMAT:
            if (arg)
            {
                if (copy_to_user(arg, &audio_pcm_format, sizeof(audio_pcm_format)) != 0)
                    return -1;
                return 0;
            }
            return -1;
            
        default:
            return -1;  /* Invalid request */
    }
#else
    (void)request; (void)arg;
    return -ENODEV;
#endif
}

int64_t dev_audio_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    /*
     * Playback-only device: no capture backend exists. Returning 0 would
     * mean EOF to read(2) callers, so report the node as unsuitable for
     * reading instead (read(2) ERRORS: EINVAL).
     */
    return -EINVAL;
}
static const devfs_ops_t audio_ops = {
    .read = dev_audio_read,
    .write = dev_audio_write,
    .ioctl = dev_audio_ioctl,
};
devfs_node_t dev_audio = {
    .entry = { .name = "audio", .mode = 0660, .device_id = 6 },
    .ops = &audio_ops,
    .ref_count = 0
};
