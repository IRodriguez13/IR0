/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: pc_speaker.h
 * Description: IR0 kernel source/header file
 */

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

