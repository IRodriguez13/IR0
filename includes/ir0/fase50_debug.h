/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fase50_debug.h
 * Description: FASE50 bring-up serial diagnostics (gated)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once


#include <config.h>

#if CONFIG_DEBUG_FASE50
#define IR0_FASE50_DBG 1
#else
#define IR0_FASE50_DBG 0
#endif

