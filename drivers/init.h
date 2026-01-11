// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Driver Initialization Header
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: drivers/init.h
 * Description: Header for driver initialization subsystem
 */

#ifndef IR0_DRIVERS_INIT_H
#define IR0_DRIVERS_INIT_H

/**
 * Initialize driver subsystem
 * This includes:
 * - Driver registry initialization
 * - Multi-language driver registration (if enabled)
 * 
 * Must be called after heap_init() and before hardware driver initialization
 */
void drivers_init(void);

#endif /* IR0_DRIVERS_INIT_H */

