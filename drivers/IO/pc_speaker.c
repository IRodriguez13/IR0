/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - PC Speaker Driver Implementation
 * Copyright (C) 2025  Iván Rodriguez
 *
 * PC speaker driver using port 0x61 and PIT channel 2
 * Simple implementation for basic audio feedback
 */

#include "pc_speaker.h"
#include <ir0/driver.h>
#include <ir0/logging.h>
#include <ir0/kmem.h>
#include <arch/common/arch_interface.h>

/* PC Speaker ports */
#define PC_SPEAKER_PORT 0x61
#define PIT_CHANNEL2_DATA 0x42
#define PIT_MODE_COMMAND 0x43

/* PC Speaker bits in port 0x61 */
#define PC_SPEAKER_GATE2 0x01 /* Gate for PIT channel 2 */
#define PC_SPEAKER_SPKR 0x02  /* Speaker enable bit */

/* PIT frequency divisor */
#define PIT_BASE_FREQ 1193180 /* PIT base frequency (1.19318 MHz) */

static bool speaker_initialized = false;
static bool speaker_enabled = false;

/* Initialize PIT channel 2 for speaker */
static void pit_channel2_set_frequency(uint16_t frequency)
{
    if (frequency == 0 || frequency > 20000)
        return;

    /* Calculate divisor */
    uint16_t divisor = PIT_BASE_FREQ / frequency;

    /* Set PIT channel 2 to mode 3 (square wave generator) */
    /* Channel 2, Access mode: lobyte/hibyte, Mode 3, Binary */
    outb(PIT_MODE_COMMAND, 0xB6);

    /* Set frequency (low byte then high byte) */
    outb(PIT_CHANNEL2_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL2_DATA, (uint8_t)((divisor >> 8) & 0xFF));
}

/* Initialize PC speaker */
static int32_t speaker_driver_init(void)
{
    if (speaker_initialized)
    {
        LOG_WARNING("PCSpeaker", "PC Speaker already initialized");
        return 0;
    }

    /* Disable speaker initially */
    uint8_t port_val = inb(PC_SPEAKER_PORT);
    port_val &= ~(PC_SPEAKER_GATE2 | PC_SPEAKER_SPKR);
    outb(PC_SPEAKER_PORT, port_val);

    speaker_initialized = true;
    speaker_enabled = false;

    LOG_INFO("PCSpeaker", "PC Speaker initialized");
    return 0;
}

/* Play beep at specified frequency */
static int32_t speaker_driver_beep(void *buf, size_t len)
{
    if (!speaker_initialized)
    {
        LOG_ERROR("PCSpeaker", "PC Speaker not initialized");
        return -1;
    }

    if (!buf || len < sizeof(uint16_t))
    {
        LOG_ERROR("PCSpeaker", "Invalid buffer or size");
        return -1;
    }

    /* Extract frequency from buffer */
    uint16_t frequency = *(uint16_t *)buf;

    if (frequency == 0)
    {
        pc_speaker_stop();
        return sizeof(uint16_t);
    }

    /* Program PIT channel 2 for frequency */
    pit_channel2_set_frequency(frequency);

    /* Enable speaker */
    uint8_t port_val = inb(PC_SPEAKER_PORT);
    port_val |= (PC_SPEAKER_GATE2 | PC_SPEAKER_SPKR);
    outb(PC_SPEAKER_PORT, port_val);

    speaker_enabled = true;

    return sizeof(uint16_t);
}

/* Stop beeping */
static void speaker_driver_stop(void)
{
    if (!speaker_initialized)
        return;

    /* Disable speaker */
    uint8_t port_val = inb(PC_SPEAKER_PORT);
    port_val &= ~(PC_SPEAKER_GATE2 | PC_SPEAKER_SPKR);
    outb(PC_SPEAKER_PORT, port_val);

    speaker_enabled = false;
}

/* Shutdown driver */
static void speaker_driver_shutdown(void)
{
    speaker_driver_stop();
    speaker_initialized = false;
    LOG_INFO("PCSpeaker", "PC Speaker shutdown");
}

/* Driver operations */
static ir0_driver_ops_t speaker_ops = {
    .init = speaker_driver_init,
    .probe = NULL,
    .remove = NULL,
    .shutdown = speaker_driver_shutdown,
    .read = NULL,
    .write = speaker_driver_beep, /* Write frequency to beep */
    .ioctl = NULL,
    .suspend = NULL,
    .resume = NULL};

/* Driver info */
static ir0_driver_info_t speaker_info = {
    .name = "pc_speaker",
    .version = "1.0",
    .author = "IR0",
    .description = "PC Speaker (Buzzer) driver - Simple audio feedback",
    .language = IR0_DRIVER_LANG_C};

/* Public API */
void pc_speaker_init(void)
{
    /* Register driver */
    ir0_driver_t *driver = ir0_register_driver(&speaker_info, &speaker_ops);
    if (!driver)
    {
        LOG_ERROR("PCSpeaker", "Failed to register PC Speaker driver");
    }
}

int32_t pc_speaker_beep(uint16_t frequency)
{
    return speaker_driver_beep(&frequency, sizeof(frequency));
}

void pc_speaker_stop(void)
{
    speaker_driver_stop();
}

void pc_speaker_set_enabled(bool enabled)
{
    if (!speaker_initialized)
        return;

    uint8_t port_val = inb(PC_SPEAKER_PORT);
    if (enabled)
    {
        port_val |= PC_SPEAKER_SPKR;
    }
    else
    {
        port_val &= ~PC_SPEAKER_SPKR;
    }
    outb(PC_SPEAKER_PORT, port_val);
    speaker_enabled = enabled;
}
