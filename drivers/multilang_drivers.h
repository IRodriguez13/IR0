// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Multi-Language Driver Registration
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: drivers/multilang_drivers.h
 * Description: Header for multi-language driver registration functions
 */

#ifndef IR0_MULTILANG_DRIVERS_H
#define IR0_MULTILANG_DRIVERS_H

#include <ir0/driver.h>

/**
 * Register all multi-language example drivers
 * This function registers Rust and C++ example drivers if they are compiled
 * 
 * @return Number of drivers successfully registered
 */
int register_multilang_example_drivers(void);

#endif /* IR0_MULTILANG_DRIVERS_H */

