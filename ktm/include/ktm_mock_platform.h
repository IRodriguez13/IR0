/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_mock_platform.h
 * Description: Test-only platform power ops mock control surface.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/platform_ops.h>

extern const struct ir0_platform_ops ktm_platform_mock_ops;

void ktm_platform_mock_reset(void);
unsigned ktm_platform_mock_halt_count(void);
unsigned ktm_platform_mock_reboot_count(void);
unsigned ktm_platform_mock_poweroff_count(void);
