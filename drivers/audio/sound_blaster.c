#include "sound_blaster.h"
#include <ir0/print.h>
#include <stddef.h>
#include <string.h>

// I/O port operations (these need to be implemented in your kernel)
extern uint8_t inb(uint16_t port);
extern void outb(uint16_t port, uint8_t data);
extern uint16_t inw(uint16_t port);
extern void outw(uint16_t port, uint16_t data);

// DMA controller functions (these need to be implemented)
extern void dma_setup_channel(uint8_t channel, uint32_t addr, uint16_t length, bool is_16bit);
extern void dma_enable_channel(uint8_t channel);
extern void dma_disable_channel(uint8_t channel);

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
    
    // Wait a bit
    for (volatile int i = 0; i < 1000; i++);
    
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
        for (volatile int i = 0; i < 100; i++);
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
    
    // Allocate memory for sample data (in a real kernel, use proper memory allocation)
    sample->data = data; // For now, just reference the provided data
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
    
    // In a real implementation, free the allocated memory
    sample->data = NULL;
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
        for (volatile int i = 0; i < 10; i++);
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