/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: klog_event.h
 * Description: Structured kernel event records and boot-phase contract.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum
{
	KLOG_CLOCK_UNAVAILABLE = 0,
	KLOG_CLOCK_RAW = 1,
	KLOG_CLOCK_CALIBRATED = 2,
	KLOG_CLOCK_MONOTONIC = 3
} klog_clock_state_t;

typedef enum
{
	KLOG_BOOT_EARLY_ARCH = 0,
	KLOG_BOOT_MEMORY,
	KLOG_BOOT_PLATFORM,
	KLOG_BOOT_TIME,
	KLOG_BOOT_INTERRUPTS,
	KLOG_BOOT_BUS_ENUMERATION,
	KLOG_BOOT_DRIVERS,
	KLOG_BOOT_STORAGE,
	KLOG_BOOT_ROOTFS,
	KLOG_BOOT_USERSPACE,
	KLOG_BOOT_READY
} klog_boot_phase_t;

typedef enum
{
	KLOG_EVENT_GENERIC = 0,
	KLOG_EVENT_BOOT_BANNER = 1,
	KLOG_EVENT_BOOT_INFO = 2,
	KLOG_EVENT_DRIVER_SUMMARY = 3,
	KLOG_EVENT_USERSPACE_HANDOFF = 4,
	KLOG_EVENT_DRIVER_PROBE_RESULT = 5,
	KLOG_EVENT_FIRST_BAREMETAL_BOOT = 6
} klog_event_id_t;

typedef struct klog_record
{
	uint64_t sequence;
	uint64_t timestamp_ns;
	uint64_t raw_ticks;
	uint32_t event_id;
	uint16_t subsystem;
	uint8_t severity;
	uint8_t clock_state;
	uint8_t boot_phase;
	uint8_t cpu_id;
	char component[24];
	char message[96];
} klog_record_t;

void klog_set_boot_phase(klog_boot_phase_t phase);
klog_boot_phase_t klog_get_boot_phase(void);
const char *klog_boot_phase_string(klog_boot_phase_t phase);
void klog_event(uint32_t event_id, uint16_t subsystem, uint8_t severity,
		const char *component, const char *message);

/* Promote the 256-record early ring after the heap is online. */
int klog_promote_normal_ring(void);
size_t klog_record_count(void);
int klog_read_records(char *buf, size_t size);

