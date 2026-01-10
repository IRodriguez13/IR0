/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - PC Speaker Driver
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Simple PC speaker/buzzer driver for x86
 * Uses port 0x61 and PIT channel 2
 */

#ifndef PC_SPEAKER_H
#define PC_SPEAKER_H

#include <stdint.h>
#include <stdbool.h>

/* Initialize PC speaker driver */
void pc_speaker_init(void);

/* Play a beep at specified frequency (Hz) */
int32_t pc_speaker_beep(uint16_t frequency);

/* Stop beeping */
void pc_speaker_stop(void);

/* Control speaker (on/off) */
void pc_speaker_set_enabled(bool enabled);

#endif /* PC_SPEAKER_H */

