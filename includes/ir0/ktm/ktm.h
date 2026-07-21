/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm.h
 * Description: Umbrella public API — Kernel Test Machine (source of truth).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/ktm/event.h>
#include <ir0/ktm/transport.h>
#include <ir0/ktm/klog.h>
#include <ir0/ktm/probe.h>
#include <ir0/ktm/snapshot.h>
#include <ir0/ktm/assert.h>
#include <ir0/ktm/checkpoint.h>
#include <ir0/ktm/scenario.h>
#include <ir0/ktm/fault.h>
#include <ir0/ktm/uapi.h>
#include <ir0/ktm/userdev.h>

void ktm_core_init(void);
int ktm_invariants_run(uint32_t mask);
void ktm_suite_reset(void);

#define KTM_INV_PROCESS  (1u << 0)
#define KTM_INV_FRAMES   (1u << 1)
#define KTM_INV_ALL      (0xffffffffu)
