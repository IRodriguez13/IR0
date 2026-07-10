/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: platform_ops.h
 * Description: Board/firmware power hooks (separate from CPU arch helpers).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

struct ir0_platform_ops
{
	void (*halt)(void);
	void (*reboot)(void);
	void (*poweroff)(void);
};

const struct ir0_platform_ops *ir0_platform_ops_get(void);
void ir0_platform_ops_set(const struct ir0_platform_ops *ops);
