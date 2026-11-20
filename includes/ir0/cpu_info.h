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

uint32_t get_cpu_id(void);
uint32_t get_cpu_count(void);
int get_cpu_vendor(char *vendor_buf);
int get_cpu_signature(uint32_t *family, uint32_t *model, uint32_t *stepping);
int get_cpuid_max_leaf(uint32_t *max_leaf);
int get_cpu_brand_string(char *buf, size_t size);
int get_cpu_feature_bits(uint32_t *out_edx, uint32_t *out_ecx);
int hypervisor_vendor(char *buf, size_t n);

enum ir0_hv_kind
{
	IR0_HV_NONE = 0,
	IR0_HV_KVM,
	IR0_HV_XEN,
	IR0_HV_OTHER
};

/*
 * Hardware probe. Returns 0 and writes *kind when the probe ran.
 * Returns -1 when the probe did not run; that is not bare metal.
 */
int hypervisor_kind(enum ir0_hv_kind *kind);

/* True only after the probe ran and reported a hypervisor. */
static inline int hypervisor_present(void)
{
	enum ir0_hv_kind kind;

	if (hypervisor_kind(&kind) < 0)
		return 0;
	return kind != IR0_HV_NONE;
}
uint32_t get_cpu_clflush_size(void);

void timer_init(void);
