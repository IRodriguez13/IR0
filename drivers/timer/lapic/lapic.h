/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: lapic.h
 * Description: IR0 kernel source/header file
 */

#pragma once
#include <stdint.h>

void lapic_init_timer(void);
int lapic_available(void);
void lapic_send_eoi(void);
