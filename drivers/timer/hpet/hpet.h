/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: hpet.h
 * Description: IR0 kernel source/header file
 */

#ifndef HPET_H
#define HPET_H

#include <stdint.h>
#include <stddef.h>

void hpet_init();
void hpet_set_address(void* addr);

#endif
