/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — FASE50 bring-up serial diagnostics (gated).
 */

#ifndef _IR0_FASE50_DEBUG_H
#define _IR0_FASE50_DEBUG_H

#include <config.h>

#if CONFIG_DEBUG_FASE50
#define IR0_FASE50_DBG 1
#else
#define IR0_FASE50_DBG 0
#endif

#endif /* _IR0_FASE50_DEBUG_H */
