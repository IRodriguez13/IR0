/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: msr.h
 * Description: x86 model-specific registers.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

uint64_t rdmsr(uint32_t msr);
void wrmsr(uint32_t msr, uint64_t value);
