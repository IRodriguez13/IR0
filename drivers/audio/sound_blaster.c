// SPDX-License-Identifier: GPL-3.0-only
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
#include <ir0/print.h>
#include <stddef.h>
#include <string.h>
#include <arch/common/arch_interface.h>
#include <interrupt/arch/io.h>
#include <drivers/dma/dma.h>

// Global Sound Blaster state
static sb16_state_t sb16_state = {0};

bool sb16_init(void)
{
    // Reset the DSP
    if (!sb16_reset_dsp()) {
        return false;
    }
    
    // Get DSP version
    sb16_state.dsp_version = sb16_get_dsp_version();
    if (sb16_state.dsp_version < 0x0400) {
        // Not a Sound Blaster 16 or compatible
        return false;
    }
    
    // Initialize default settings
    sb16_state.initialized = true;
    sb16_state.speaker_enabled = false;
    sb16_state.master_volume = 0x88; // Medium volume
    sb16_state.pcm_volume = 0x88;
    sb16_state.current_sample_rate = 22050;
    sb16_state.current_format = SB16_FORMAT_8BIT_MONO;
    sb16_state.current_sample = NULL;
    
    // Set initial mixer values
    sb16_set_master_volume(sb16_state.master_volume);
    sb16_set_pcm_volume(sb16_state.pcm_volume);
    
    // Turn on speaker
    sb16_speaker_on();
    
    return true;
}

void sb16_shutdown(void)
{
    if (!sb16_state.initialized) {
        return;
    }
    
    // Stop any current playback
    sb16_stop_playback();
    
    // Turn off speaker
    sb16_speaker_off();
    
    // Reset DSP
    sb16_reset_dsp();
    
    sb16_state.initialized = false;
}

bool sb16_is_available(void)
{
    return sb16_state.initialized;
}

bool sb16_reset_dsp(void)
{
    // Send reset command
    outb(SB16_RESET_PORT, 1);
    
    // Wait for reset to take effect (1ms)
    extern void udelay(uint32_t microseconds);
    udelay(1000);
    
    // Clear reset
    outb(SB16_RESET_PORT, 0);
    
    // Wait for DSP ready (should return 0xAA)
    int timeout = 1000;
    while (timeout-- > 0) {
        if (sb16_dsp_ready_read()) {
            uint8_t response = sb16_dsp_read();
            if (response == 0xAA) {
                return true;
            }
        }
        udelay(100); // Wait 100 microseconds
    }
    
    return false;
}

uint16_t sb16_get_dsp_version(void)
{
    if (!sb16_dsp_write(SB16_DSP_GET_VERSION)) {
        return 0;
    }
    
    // Wait for response
    int timeout = 1000;
    while (timeout-- > 0 && !sb16_dsp_ready_read());
    
    if (timeout <= 0) {
        return 0;
    }
    
    uint8_t major = sb16_dsp_read();
    
    timeout = 1000;
    while (timeout-- > 0 && !sb16_dsp_ready_read());
    
    if (timeout <= 0) {
        return 0;
    }
    
    uint8_t minor = sb16_dsp_read();
    
    return (major << 8) | minor;
}

void sb16_set_master_volume(uint8_t volume)
{
    sb16_mixer_write(SB16_MIXER_MASTER_VOL, volume);
    sb16_state.master_volume = volume;
}

uint8_t sb16_get_master_volume(void)
{
    return sb16_state.master_volume;
}

void sb16_set_pcm_volume(uint8_t volume)
{
    sb16_mixer_write(SB16_MIXER_PCM_VOL, volume);
    sb16_state.pcm_volume = volume;
}

uint8_t sb16_get_pcm_volume(void)
{
    return sb16_state.pcm_volume;
}

void sb16_speaker_on(void)
{
    sb16_dsp_write(SB16_DSP_SPEAKER_ON);
    sb16_state.speaker_enabled = true;
}

void sb16_speaker_off(void)
{
    sb16_dsp_write(SB16_DSP_SPEAKER_OFF);
    sb16_state.speaker_enabled = false;
}

bool sb16_is_speaker_on(void)
{
    return sb16_state.speaker_enabled;
}

int sb16_create_sample(sb16_sample_t *sample, uint8_t *data, uint32_t size, 
                       uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample)
{
    if (!sample || !data || size == 0) {
        return -1;
    }
    
    // Allocate memory for sample data
    extern void *kmalloc(size_t size);
    sample->data = (uint8_t*)kmalloc(size);
    if (!sample->data) {
        return -1; // Memory allocation failed
    }
    
    // Copy the audio data to our allocated buffer
    extern void *memcpy(void *dest, const void *src, size_t n);
    memcpy(sample->data, data, size);
    sample->size = size;
    sample->sample_rate = sample_rate;
    sample->channels = channels;
    sample->bits_per_sample = bits_per_sample;
    sample->is_playing = false;
    
    // Determine format
    if (bits_per_sample == 8) {
        sample->format = (channels == 1) ? SB16_FORMAT_8BIT_MONO : SB16_FORMAT_8BIT_STEREO;
    } else {
        sample->format = (channels == 1) ? SB16_FORMAT_16BIT_MONO : SB16_FORMAT_16BIT_STEREO;
    }
    
    return 0;
}

void sb16_destroy_sample(sb16_sample_t *sample)
{
    if (!sample) {
        return;
    }
    
    // Stop playback if this sample is playing
    if (sample->is_playing) {
        sb16_stop_playback();
    }
    
    // Free the allocated memory
    if (sample->data) {
        extern void kfree(void *ptr);
        kfree(sample->data);
        sample->data = NULL;
    }
    sample->size = 0;
    sample->is_playing = false;
}

int sb16_play_sample(sb16_sample_t *sample)
{
    if (!sb16_state.initialized || !sample || !sample->data) {
        return -1;
    }
    
    // Stop any current playback
    sb16_stop_playback();
    
    // Set sample rate
    if (!sb16_dsp_write(SB16_DSP_SET_SAMPLE_RATE)) {
        return -1;
    }
    if (!sb16_dsp_write((sample->sample_rate >> 8) & 0xFF)) {
        return -1;
    }
    if (!sb16_dsp_write(sample->sample_rate & 0xFF)) {
        return -1;
    }
    
    // Setup DMA
    if (sample->bits_per_sample == 8) {
        sb16_setup_dma_8bit((uintptr_t)sample->data, sample->size - 1);
    } else {
        sb16_setup_dma_16bit((uintptr_t)sample->data, (sample->size / 2) - 1);
    }
    
    // Start playback
    if (sample->bits_per_sample == 8) {
        if (!sb16_dsp_write(SB16_DSP_PLAY_8BIT)) {
            return -1;
        }
        if (!sb16_dsp_write((sample->size - 1) & 0xFF)) {
            return -1;
        }
        if (!sb16_dsp_write(((sample->size - 1) >> 8) & 0xFF)) {
            return -1;
        }
    } else {
        if (!sb16_dsp_write(SB16_DSP_PLAY_16BIT)) {
            return -1;
        }
        uint16_t length = (sample->size / 2) - 1;
        if (!sb16_dsp_write(length & 0xFF)) {
            return -1;
        }
        if (!sb16_dsp_write((length >> 8) & 0xFF)) {
            return -1;
        }
    }
    
    sample->is_playing = true;
    sb16_state.current_sample = sample;
    
    return 0;
}

int sb16_stop_playback(void)
{
    if (!sb16_state.initialized) {
        return -1;
    }
    
    if (sb16_state.current_sample) {
        if (sb16_state.current_sample->bits_per_sample == 8) {
            dma_disable_channel(SB16_DMA_8BIT);
        } else {
            dma_disable_channel(SB16_DMA_16BIT);
        }
        
        sb16_state.current_sample->is_playing = false;
        sb16_state.current_sample = NULL;
    }
    
    return 0;
}

int sb16_pause_playback(void)
{
    if (!sb16_state.initialized || !sb16_state.current_sample) {
        return -1;
    }
    
    if (sb16_state.current_sample->bits_per_sample == 8) {
        sb16_dsp_write(SB16_DSP_PAUSE_8BIT);
    } else {
        sb16_dsp_write(SB16_DSP_PAUSE_16BIT);
    }
    
    return 0;
}

int sb16_resume_playback(void)
{
    if (!sb16_state.initialized || !sb16_state.current_sample) {
        return -1;
    }
    
    if (sb16_state.current_sample->bits_per_sample == 8) {
        sb16_dsp_write(SB16_DSP_RESUME_8BIT);
    } else {
        sb16_dsp_write(SB16_DSP_RESUME_16BIT);
    }
    
    return 0;
}

bool sb16_is_playing(void)
{
    return sb16_state.current_sample && sb16_state.current_sample->is_playing;
}

bool sb16_dsp_write(uint8_t data)
{
    int timeout = 1000;
    
    // Wait for DSP to be ready for write
    while (timeout-- > 0) {
        if (sb16_dsp_ready_write()) {
            outb(SB16_WRITE_DATA, data);
            return true;
        }
        udelay(10); // Wait 10 microseconds
    }
    
    return false;
}

uint8_t sb16_dsp_read(void)
{
    return inb(SB16_READ_DATA);
}

bool sb16_dsp_ready_write(void)
{
    return (inb(SB16_WRITE_DATA) & 0x80) == 0;
}

bool sb16_dsp_ready_read(void)
{
    return (inb(SB16_READ_STATUS) & 0x80) != 0;
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

void sb16_setup_dma_8bit(uint32_t buffer_addr, uint16_t length)
{
    dma_setup_channel(SB16_DMA_8BIT, buffer_addr, length, false);
    dma_enable_channel(SB16_DMA_8BIT);
}

void sb16_setup_dma_16bit(uint32_t buffer_addr, uint16_t length)
{
    dma_setup_channel(SB16_DMA_16BIT, buffer_addr, length, true);
    dma_enable_channel(SB16_DMA_16BIT);
}

void sb16_irq_handler(void)
{
    // Acknowledge the interrupt
    if (sb16_state.current_sample) {
        if (sb16_state.current_sample->bits_per_sample == 8) {
            inb(SB16_READ_STATUS);
        } else {
            inb(SB16_ACK_16BIT);
        }
    }
    
    // Handle end of playback
    if (sb16_state.current_sample) {
        sb16_state.current_sample->is_playing = false;
        sb16_state.current_sample = NULL;
    }
}

/**
 * Microsecond delay function for Sound Blaster timing
 * Uses CPU timing for precise delays
 */
void udelay(uint32_t microseconds) {
    // Detect CPU frequency using PIT timer calibration
    static uint64_t cpu_freq_mhz = 0;
    
    if (cpu_freq_mhz == 0) {
        // Use PIT timer for calibration (1.193182 MHz base frequency)
        const uint32_t pit_frequency = 1193182;
        const uint16_t pit_divisor = 1193; // ~1ms interval
        
        // Program PIT channel 0 for one-shot mode
        outb(0x43, 0x30); // Channel 0, lobyte/hibyte, mode 0
        outb(0x40, pit_divisor & 0xFF);
        outb(0x40, (pit_divisor >> 8) & 0xFF);
        
        // Read initial TSC
        uint64_t tsc_start, tsc_end;
        __asm__ volatile("rdtsc" : "=A"(tsc_start));
        
        // Wait for PIT to count down
        uint16_t pit_count;
        do {
            outb(0x43, 0x00); // Latch count
            pit_count = inb(0x40);
            pit_count |= (inb(0x40) << 8);
        } while (pit_count > 100); // Wait until near zero
        
        // Read final TSC
        __asm__ volatile("rdtsc" : "=A"(tsc_end));
        
        // Calculate frequency
        uint64_t tsc_cycles = tsc_end - tsc_start;
        uint64_t time_us = (pit_divisor * 1000000ULL) / pit_frequency;
        cpu_freq_mhz = (tsc_cycles * 1000000ULL) / (time_us * 1000000ULL);
        
        // Sanity check: frequency should be between 100MHz and 10GHz
        if (cpu_freq_mhz < 100 || cpu_freq_mhz > 10000) {
            // Fallback to reasonable default
            cpu_freq_mhz = 2000; // 2GHz default
        }
    }
    uint64_t cycles = (uint64_t)microseconds * cpu_freq_mhz;
    
    // Use RDTSC for precise timing
    uint64_t start_tsc, current_tsc;
    
    // Read Time Stamp Counter
    __asm__ volatile("rdtsc" : "=A"(start_tsc));
    
    do {
        __asm__ volatile("rdtsc" : "=A"(current_tsc));
        __asm__ volatile("pause"); // CPU hint for spin loops
    } while ((current_tsc - start_tsc) < cycles);
}