/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_info.h
 * Description: Architecture bring-up and identity facades (uname, cmdline, init).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

void early_init(void);
void late_init(void);
void syscall_init(void);

const char *get_cmdline(void);
const char *get_arch_name(void);
const char *get_arch_uname_machine(void);
uint32_t get_arch_bits(void);
int supports_feature(const char *feature);
const char *get_arch_cflags(void);
const char *get_arch_ldflags(void);

void dump_cpu_registers(void);
