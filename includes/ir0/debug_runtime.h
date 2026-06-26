/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: debug_runtime.h
 * Description: IR0 kernel header — debug runtime
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#ifndef IR0_DEBUG_PMM
#define IR0_DEBUG_PMM 0
#endif

#ifndef IR0_DEBUG_WAIT
#define IR0_DEBUG_WAIT 0
#endif

#ifndef IR0_DEBUG_PROC
#define IR0_DEBUG_PROC 0
#endif

/*
 * D1.17 FASE40_D diagnostic only — wait/reap/destroy/unmap/PMM trail.
 * Remove or set to 0 after root-cause is confirmed.
 */
#ifndef FASE40_D_AUDIT
#define FASE40_D_AUDIT 0
#endif

#if FASE40_D_AUDIT
#define FASE40_D_AUDIT_LOG(stmt) do { stmt; } while (0)
#else
#define FASE40_D_AUDIT_LOG(stmt) ((void)0)
#endif

#if IR0_DEBUG_PMM
#define IR0_DBG_PMM(stmt) do { stmt; } while (0)
#else
#define IR0_DBG_PMM(stmt) ((void)0)
#endif

#if IR0_DEBUG_WAIT
#define IR0_DBG_WAIT(stmt) do { stmt; } while (0)
#else
#define IR0_DBG_WAIT(stmt) ((void)0)
#endif

#if IR0_DEBUG_PROC
#define IR0_DBG_PROC(stmt) do { stmt; } while (0)
#else
#define IR0_DBG_PROC(stmt) ((void)0)
#endif
