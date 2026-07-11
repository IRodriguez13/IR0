/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fault.h
 * Description: KTM fault injection stub (v1 API; chaos = v2).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <config.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum ktm_fault_mode
{
	KTM_FAULT_ONCE = 1,
	KTM_FAULT_AFTER_N,
	KTM_FAULT_EVERY_N,
	KTM_FAULT_PROBABILITY
} ktm_fault_mode_t;

void ktm_fault_seed(uint32_t seed);
int ktm_fault_configure(const char *name, ktm_fault_mode_t mode, uint32_t value);
bool ktm_fault_should_fail(const char *name);

#if defined(CONFIG_KTM_FAULT) && CONFIG_KTM_FAULT
#define KTM_FAULT_POINT(name) \
	do { \
		if (ktm_fault_should_fail(name)) \
			; \
	} while (0)
#else
#define KTM_FAULT_POINT(name) do { } while (0)
#endif
