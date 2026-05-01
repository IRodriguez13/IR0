/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: user_mode.h
 * Description: x86-specific optional exports; portable code uses arch_portable.h only.
 */

#pragma once
#include <stdint.h>

void syscall_handler_c(void);
