/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 runtime debug gates — off by default; enable via make:
 *   IR0_DEBUG_PMM=1 IR0_DEBUG_WAIT=1 IR0_DEBUG_PROC=1
 */

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
