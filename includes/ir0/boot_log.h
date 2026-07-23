/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: boot_log.h
 * Description: Portable boot serial contract — same framed lines on every ISA.
 *
 * After serial is ready, call ir0_boot_serial_ready() first. That emits the
 * standard BOOT banner as the first framed line. Later lines use COMP BOOT
 * (universal), ARCH (ISA/board detail), or SMOKE (autokill tags).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Call once after serial_init() (or arch UART init). Releases boot hold and
 * prints: [ts] [INFO] [BOOT] IR0 Kernel v<ver> Boot routine
 */
void ir0_boot_serial_ready(void);

/* Framed INFO / WARN — same layout as klog human channel. */
void ir0_boot_info(const char *component, const char *message);
void ir0_boot_warn(const char *component, const char *message);

/* ISA/board detail under COMP ARCH (does not replace the BOOT banner). */
void ir0_boot_arch(const char *message);

/* Autokill / stage tags under COMP SMOKE (tag remains greppable as substring). */
void ir0_boot_smoke(const char *tag);

#ifdef __cplusplus
}
#endif
