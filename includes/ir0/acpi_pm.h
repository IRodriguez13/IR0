/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: acpi_pm.h
 * Description: Facade — ACPI FADT PM1a/b poweroff + DSDT _S5_ SLP_TYP.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/**
 * Discover FADT, cache PM1a/PM1b, and optionally parse DSDT _S5_.
 * Safe to call more than once; returns 0 if PM1a is known.
 */
int ir0_acpi_pm_init(void);

/**
 * Attempt ACPI S5 soft-off via PM1a_CNT ((SLP_TYP<<10)|SLP_EN).
 * Prints ACPI_S5_OK at init when _S5_ is found; ACPI_S5_POWEROFF +
 * ACPI_PM1A_POWEROFF before the I/O write when using parsed typ.
 * Does not return on success (hardware/QEMU should power off).
 */
int ir0_acpi_pm_try_poweroff(void);
