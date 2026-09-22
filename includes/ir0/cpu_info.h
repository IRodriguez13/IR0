/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cpu_info.h
 * Description: CPU identity, idle/power, and PIT-style timer facades.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

void cpu_idle(void);
void system_halt(void) __attribute__((noreturn));
void system_reboot(void) __attribute__((noreturn));
void system_poweroff(void) __attribute__((noreturn));

size_t get_page_size(void);
uint32_t get_cpu_id(void);
uint32_t get_cpu_count(void);
uint32_t get_cpu_mode(void);
int get_cpu_vendor(char *vendor_buf);
int get_cpu_signature(uint32_t *family, uint32_t *model, uint32_t *stepping);
int get_cpuid_max_leaf(uint32_t *max_leaf);
int get_cpu_brand_string(char *buf, size_t size);
int get_cpu_feature_bits(uint32_t *out_edx, uint32_t *out_ecx);
int hypervisor_present(void);
int hypervisor_vendor(char *buf, size_t n);
uint32_t get_cpu_clflush_size(void);

void timer_init(void);
void timer_set_frequency(uint32_t hz);
uint32_t timer_get_frequency(void);
