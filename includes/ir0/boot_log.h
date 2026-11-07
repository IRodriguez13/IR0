/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: boot_log.h
 * Description: Portable, banner-first boot log facade.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Call once after serial_init() to emit the boot banner. */
void ir0_boot_serial_ready(void);

/* Framed boot events. */
void ir0_boot_info(const char *component, const char *message);
void ir0_boot_notice(const char *component, const char *message);
void ir0_boot_warn(const char *component, const char *message);

/* Structured early diagnostic. */
void ir0_boot_info_hex64(const char *component, const char *label,
			 uint64_t value);

/* ISA/board detail. */
void ir0_boot_arch(const char *message);

/* Greppable boot stage tag. */
void ir0_boot_smoke(const char *tag);

/* Hypervisor or bare-metal identity. */
void ir0_boot_log_platform_hypervisor(void);

#ifdef __cplusplus
}
#endif
