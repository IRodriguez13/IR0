/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: driver_backend.h
 * Description: Build-selected driver backend — avoid ISA #if in portable drivers/.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/*
 * x86-64 product: legacy PC class devices (i8042, VGA text MMIO, PIT/HPET/LAPIC).
 * ARM64: Makefile leaves this undefined → 0.
 */
#ifndef IR0_LEGACY_PC_IO
#define IR0_LEGACY_PC_IO 0
#endif
