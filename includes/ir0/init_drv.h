/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Driver initialization facade (opaque; no drivers/ include).
 */

#pragma once

/**
 * Initialize driver registry / multi-language registration.
 * Call after heap_init() and before hardware driver init.
 */
void drivers_init(void);

/**
 * Initialize all registered hardware drivers (IRQ/I/O registration).
 * Call after drivers_init().
 */
void init_all_drivers(void);
