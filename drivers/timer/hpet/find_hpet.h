/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: find_hpet.h
 * Description: IR0 kernel source/header file
 */

#include <drivers/timer/acpi/acpi.h>
#include <stddef.h>
#include <stdint.h>


#define ACPI_RSDP_SIGNATURE "RSD PTR "
#define ACPI_HPET_SIGNATURE 0x54455048 
#define RSDP_SEARCH_START 0x000E0000
#define RSDP_SEARCH_END   0x000FFFFF




