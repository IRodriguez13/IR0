/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: acpi_pm.h
 * Description: Facade — ACPI FADT PM1a/b poweroff (QEMU soft-off, no AML).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/**
 * Discover FADT and cache PM1a/PM1b control ports.
 * Safe to call more than once; returns 0 if PM1a is known.
 */
int ir0_acpi_pm_init(void);

/**
 * Attempt ACPI S5 soft-off via PM1a_CNT (SLP_TYP=0 | SLP_EN).
 * Prints ACPI_PM1A_POWEROFF before the I/O write.
 * Returns -1 if FADT/PM1a unavailable (caller should fallback).
 * Does not return on success (hardware/QEMU should power off).
 */
int ir0_acpi_pm_try_poweroff(void);
