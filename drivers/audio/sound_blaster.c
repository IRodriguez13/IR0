/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sound_blaster.c
 * Description: Sound Blaster 16 audio driver with DMA support and 8/16-bit playback
 */

#include "sound_blaster.h"
#include <ir0/vga.h>
#include <stddef.h>
#include <string.h>
#include <arch/common/arch_interface.h>
#include <interrupt/arch/io.h>
#include <drivers/dma/dma.h>
#include <ir0/kmem.h>
#include <drivers/timer/pit/pit.h>
#include <ir0/driver.h>
#include <ir0/logging.h>
#include <kernel/resource_registry.h>

/* Global Sound Blaster state */
static sb16_state_t sb16_state = {0};

/* Forward declarations */
static int32_t sb16_hw_init(void);

/* Driver registration structures */
static ir0_driver_ops_t sb16_ops = {
    .init = sb16_hw_init,
    .shutdown = sb16_shutdown
};

static ir0_driver_info_t sb16_info = {
    .name = "Sound Blaster 16",
    .version = "1.0",
    .author = "Iván Rodriguez",
    .description = "ISA Sound Blaster 16 Audio Driver",
    .language = IR0_DRIVER_LANG_C
};

/**
 * sb16_init - register Sound Blaster 16 driver
 */
bool sb16_init(void)
{
    LOG_INFO("SB16", "Registering Sound Blaster 16 driver...");
    ir0_register_driver(&sb16_info, &sb16_ops);
    return true;
}

static int32_t sb16_hw_init(void)
{
    LOG_INFO("SB16", "Initializing Sound Blaster 16 hardware...");

    /* Reset the DSP */
    if (!sb16_reset_dsp())
    {
        LOG_ERROR("SB16", "Failed to reset DSP");
        return -1;
    }

    /* Check DSP version */
    uint16_t version = sb16_get_dsp_version();
    if (version == 0)
    {
        LOG_ERROR("SB16", "Failed to get DSP version");
        return -1;
    }

    sb16_state.dsp_version = version;
    LOG_INFO_FMT("SB16", "DSP Version %d.%d detected", version >> 8, version & 0xFF);

    /* Set default volume */
    sb16_set_master_volume(SB16_MIXER_VOL_MEDIUM);

    sb16_state.initialized = true;
    resource_register_ioport(SB16_BASE_PORT, SB16_ACK_16BIT, "sound blaster");
    return 0;
}

void sb16_shutdown(void)
{
    if (!sb16_state.initialized)
    {
        return;
    }

    /* Reset DSP to stop any playback */
    sb16_reset_dsp();

    sb16_state.initialized = false;
}

bool sb16_is_available(void)
{
    return sb16_state.initialized;
}

bool sb16_reset_dsp(void)
{
    /* Write 1 to reset port */
    outb(SB16_RESET_PORT, 1);
    
    /* Wait 3 microseconds (or more to be safe) */
    for (volatile int i = 0; i < 10000; i++); 

    /* Write 0 to reset port */
    outb(SB16_RESET_PORT, 0);

    /* Wait for DSP to be ready (0xAA) */
    int timeout = 1000;
    while (timeout--)
    {
        if (sb16_dsp_ready_read())
        {
            if (inb(SB16_READ_DATA) == SB16_DSP_READY)
            {
                return true;
            }
        }
    }

    return false;
}

bool sb16_dsp_write(uint8_t data)
{
    if (!sb16_dsp_ready_write())
    {
        return false;
    }

    outb(SB16_WRITE_DATA, data);
    return true;
}

uint8_t sb16_dsp_read(void)
{
    if (!sb16_dsp_ready_read())
    {
        return 0;
    }

    return inb(SB16_READ_DATA);
}

bool sb16_dsp_ready_read(void)
{
    int timeout = SB16_DSP_TIMEOUT;
    while (timeout--)
    {
        if (inb(SB16_READ_STATUS) & 0x80)
        {
            return true;
        }
    }
    return false;
}

bool sb16_dsp_ready_write(void)
{
    int timeout = SB16_DSP_TIMEOUT;
    while (timeout--)
    {
        if (!(inb(SB16_WRITE_DATA) & 0x80))
        {
            return true;
        }
    }
    return false;
}

uint16_t sb16_get_dsp_version(void)
{
    if (!sb16_dsp_write(SB16_DSP_GET_VERSION))
    {
        return 0;
    }

    uint8_t major = sb16_dsp_read();
    uint8_t minor = sb16_dsp_read();

    return (uint16_t)((major << 8) | minor);
}

void sb16_set_master_volume(uint8_t volume)
{
    sb16_mixer_write(SB16_MIXER_MASTER_VOL, volume);
}

void sb16_mixer_write(uint8_t reg, uint8_t data)
{
    outb(SB16_MIXER_PORT, reg);
    outb(SB16_MIXER_DATA, data);
}

uint8_t sb16_mixer_read(uint8_t reg)
{
    outb(SB16_MIXER_PORT, reg);
    return inb(SB16_MIXER_DATA);
}

void sb16_speaker_on(void)
{
    sb16_dsp_write(SB16_DSP_SPEAKER_ON);
}

void sb16_speaker_off(void)
{
    sb16_dsp_write(SB16_DSP_SPEAKER_OFF);
}

/*
 * sb16_setup_dma_8bit - Configure DMA channel 1 for 8-bit playback.
 * Buffer must be below 16MB physical (kernel heap is identity-mapped).
 */
void sb16_setup_dma_8bit(uint32_t buffer_addr, uint16_t length)
{
    dma_disable_channel(SB16_DMA_8BIT);
    dma_setup_channel(SB16_DMA_8BIT, buffer_addr, length, false);
    dma_enable_channel(SB16_DMA_8BIT);
}

/*
 * sb16_create_sample - Allocate buffer, copy PCM data, fill sample struct.
 * Caller's data is copied; buffer stays valid until sb16_destroy_sample.
 */
int sb16_create_sample(sb16_sample_t *sample, uint8_t *data, uint32_t size,
                       uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample)
{
    if (!sample || !data || size == 0)
        return -1;
    uint8_t *buf = (uint8_t *)kmalloc(size);
    if (!buf)
        return -1;
    memcpy(buf, data, size);
    sample->data = buf;
    sample->size = size;
    sample->sample_rate = sample_rate;
    sample->channels = channels;
    sample->bits_per_sample = bits_per_sample;
    sample->format = (bits_per_sample == 16) ? SB16_FORMAT_16BIT_MONO : SB16_FORMAT_8BIT_MONO;
    sample->is_playing = false;
    return 0;
}

void sb16_destroy_sample(sb16_sample_t *sample)
{
    if (sample && sample->data)
    {
        kfree(sample->data);
        sample->data = NULL;
    }
}

/*
 * sb16_play_sample - Play PCM via DMA. Blocks until transfer starts.
 * Uses 8-bit mono; sample_rate sets DSP time constant.
 */
int sb16_play_sample(sb16_sample_t *sample)
{
    if (!sample || !sample->data || sample->size == 0)
        return -1;
    if (sample->size > 0xFFFE)
        return -1;  /* DMA 8-bit max ~64KB */
    sb16_speaker_on();
    /* Time constant: 256 - 1000000/sample_rate. 22050 Hz -> 211 */
    uint32_t sr = sample->sample_rate;
    if (sr < 4000) sr = 4000;
    if (sr > 45454) sr = 45454;
    uint8_t tc = (uint8_t)(256 - (1000000 / sr));
    if (!sb16_dsp_write(SB16_DSP_SET_TIME_CONST) || !sb16_dsp_write(tc))
        return -1;
    /* 8-bit single-cycle DMA: 0x14, length low, length high */
    uint16_t len = (uint16_t)sample->size;
    if (!sb16_dsp_write(SB16_DSP_PLAY_8BIT))
        return -1;
    if (!sb16_dsp_write(len & 0xFF) || !sb16_dsp_write((len >> 8) & 0xFF))
        return -1;
    /* Physical address (identity-mapped kernel heap) */
    uint32_t phys = (uint32_t)(uintptr_t)sample->data;
    sb16_setup_dma_8bit(phys, len);
    sample->is_playing = true;
    return 0;
}