/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: acpi_rsdp.h
 * Description: ACPI Root System Description Pointer layout and validation.
 *
 * The firmware hands the RSDP physical address to the kernel through the
 * boot snapshot (struct ir0_boot_snapshot::acpi_rsdp, the analogue of Linux
 * boot_params.acpi_rsdp_addr). ACPI consumers read it from there and use
 * this helper to check the structure before walking the RSDT/XSDT.
 *
 * Reference: ACPI 6.5 §5.2.5.3 Root System Description Pointer Structure.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define ACPI_RSDP_SIGNATURE "RSD PTR "
/* ACPI 1.0 RSDP: the checksum covers the first 20 bytes. */
#define ACPI_RSDP_V1_SIZE 20U

struct acpi_rsdp
{
	char signature[8];
	uint8_t checksum;
	char oem_id[6];
	uint8_t revision;
	uint32_t rsdt_address;
	/* ACPI 2.0+ only. */
	uint32_t length;
	uint64_t xsdt_address;
	uint8_t ext_checksum;
	uint8_t reserved[3];
} __attribute__((packed));

/* Non-zero when the bytes at @rsdp carry the signature and v1 checksum. */
static inline int acpi_rsdp_valid(const struct acpi_rsdp *rsdp)
{
	const uint8_t *bytes = (const uint8_t *)rsdp;
	const char *signature = ACPI_RSDP_SIGNATURE;
	uint8_t sum = 0;
	size_t i;

	if (!rsdp)
		return 0;
	for (i = 0; i < sizeof(rsdp->signature); i++)
	{
		if (rsdp->signature[i] != signature[i])
			return 0;
	}
	for (i = 0; i < ACPI_RSDP_V1_SIZE; i++)
		sum = (uint8_t)(sum + bytes[i]);
	return sum == 0;
}
