/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: simplefs.h
 * Description: Minimal persistent-style filesystem backend registration.
 */

#pragma once

int simplefs_register(void);
int simplefs_engine_register(const char *fs_name, int strict_83_names);
