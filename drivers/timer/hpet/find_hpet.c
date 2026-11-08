/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: find_hpet.c
 * Description: IR0 kernel source/header file
 */

#include "find_hpet.h"
#include <ir0/acpi_rsdp.h>
#include <ir0/boot_memory.h>

static int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;

    for (size_t i = 0; i < n; i++)
    {
        if (p1[i] != p2[i])
        {
            return p1[i] - p2[i];
        }
    }
    return 0;
}

// Validar si una dirección parece válida para ACPI
static int is_valid_acpi_address(uintptr_t addr)
{
    // Verificar que esté en rangos válidos de memoria
    if (addr < 0x100000) // Menor a 1MB
        return 0;
    if (addr > 0xFFFFFFFF) // Mayor a 4GB (en 32-bit)
        return 0;
    if (addr & 0xF) // No alineado a 16 bytes
        return 0;
    return 1;
}

/*
 * Walk the RSDT behind the RSDP recorded in the boot snapshot and return the
 * HPET description table, or NULL when the firmware exposes none.
 */
void *find_hpet_table(void)
{
    const struct ir0_boot_snapshot *snapshot = boot_memory_snapshot();
    const struct acpi_rsdp *rsdp;
    acpi_sdt_header_t *rsdt;
    uint32_t *table_ptrs;
    int entries;

    if (!snapshot || snapshot->acpi_rsdp == 0)
        return NULL;
    rsdp = (const struct acpi_rsdp *)(uintptr_t)snapshot->acpi_rsdp;
    if (!acpi_rsdp_valid(rsdp) || !is_valid_acpi_address(rsdp->rsdt_address))
        return NULL;

    rsdt = (acpi_sdt_header_t *)(uintptr_t)rsdp->rsdt_address;
    if (rsdt->length < sizeof(acpi_sdt_header_t) ||
        memcmp(rsdt->signature, "RSDT", 4) != 0)
        return NULL;

    entries = (rsdt->length - sizeof(acpi_sdt_header_t)) / 4;
    /* Bound the walk: a corrupt length must not run through memory. */
    if (entries > 64)
        return NULL;

    table_ptrs = (uint32_t *)((uint8_t *)rsdt + sizeof(acpi_sdt_header_t));
    for (int i = 0; i < entries; i++)
    {
        acpi_sdt_header_t *table;

        if (!is_valid_acpi_address(table_ptrs[i]))
            continue;
        table = (acpi_sdt_header_t *)(uintptr_t)table_ptrs[i];
        if (memcmp(table->signature, "HPET", 4) == 0)
            return table;
    }

    return NULL;
}
