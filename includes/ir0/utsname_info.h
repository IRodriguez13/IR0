/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: utsname_info.h
 * Description: Runtime fillers for uname(2) /proc version fields.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>

/*
 * Fill version string: "<UP|SMP> <RR|Priority>" from online CPUs and
 * active scheduler policy (not compile-time macros alone).
 */
void ir0_utsname_fill_version(char *dst, size_t n);

/* Fill nodename from /sys/kernel/hostname registry (no trailing newline). */
void ir0_utsname_fill_nodename(char *dst, size_t n);
