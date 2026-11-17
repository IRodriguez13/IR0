/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: debugreg.h
 * Description: x86 debug registers DR0-DR7.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

void debug_reg_write(unsigned int regno, uint64_t value);
uint64_t debug_reg_read(unsigned int regno);
