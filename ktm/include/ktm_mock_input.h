/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_mock_input.h
 * Description: Test-only input facade mock control surface.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <ir0/input_backend.h>

void ktm_input_mock_reset(void);

/* Configuration and stimulus. */
void ktm_input_mock_set_mouse_available(int on);
void ktm_input_mock_set_mouse_sensitivity_result(int result);
void ktm_input_mock_set_mouse_state(const ir0_mouse_state_t *state);
void ktm_input_mock_queue_key(char c);
void ktm_input_mock_queue_mouse_packet(const uint8_t packet[3]);
void ktm_input_mock_set_layout(int layout);
void ktm_input_mock_set_console_mode(int mode);

/* Observed state. */
int ktm_input_mock_mouse_interrupts(void);
int ktm_input_mock_mouse_feed_bytes(void);
uint8_t ktm_input_mock_last_aux_byte(void);
int ktm_input_mock_sensitivity(void);
int ktm_input_mock_resync_calls(void);
int ktm_input_mock_poll_calls(void);
int ktm_input_mock_wake_calls(void);
