/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: acpi_pm.c
 * Description: Minimal ACPI FADT parse for PM1a/b poweroff (no AML/_S5).
 *
 * Only walks tables that are either inside the early identity map (0..48MiB)
 * or mapped on demand via 4KiB supervisor identity (no boot-map expand).
 * QEMU often places RSDT/FADT higher; on-demand map prefers real PM1a over
 * the 0x604 fallback when tables are reachable.
 *
 * References:
 * - UEFI ACPI 6.5 §16 Waking and Sleeping
 * - OSDev ACPI poweroff (PM1a_CNT + SLP_EN)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/acpi_pm.h>
#include <ir0/serial_io.h>
#include <ir0/paging.h>
#include <stddef.h>
#include <stdint.h>

#define ACPI_RSDP_SIG "RSD PTR "
#define ACPI_RSDP_SEARCH_START 0x000E0000u
#define ACPI_RSDP_SEARCH_END   0x00100000u
#define ACPI_SLP_EN            (1u << 13)
/* Early boot identity map covers 0..48MiB; above that we map on demand. */
#define ACPI_SAFE_PHYS_MAX     0x03000000ull
/* Refuse absurd table addresses (QEMU ACPI lives well below 4GiB). */
#define ACPI_MAP_PHYS_CAP      0x100000000ull
#define ACPI_MAP_MAX_BYTES     (256u * 1024u)

typedef struct
{
	char signature[8];
	uint8_t checksum;
	char oem_id[6];
	uint8_t revision;
	uint32_t rsdt_address;
	uint32_t length;
	uint64_t xsdt_address;
	uint8_t ext_checksum;
	uint8_t reserved[3];
} __attribute__((packed)) acpi_rsdp_ext_t;

typedef struct
{
	char signature[4];
	uint32_t length;
	uint8_t revision;
	uint8_t checksum;
	char oem_id[6];
	char oem_table_id[8];
	uint32_t oem_revision;
	uint32_t creator_id;
	uint32_t creator_revision;
} __attribute__((packed)) acpi_sdt_header_t;

typedef struct
{
	acpi_sdt_header_t header;
	uint32_t firmware_ctrl;
	uint32_t dsdt;
	uint8_t reserved0;
	uint8_t preferred_pm_profile;
	uint16_t sci_int;
	uint32_t smi_cmd;
	uint8_t acpi_enable;
	uint8_t acpi_disable;
	uint8_t s4bios_req;
	uint8_t pstate_cnt;
	uint32_t pm1a_evt_blk;
	uint32_t pm1b_evt_blk;
	uint32_t pm1a_cnt_blk;
	uint32_t pm1b_cnt_blk;
} __attribute__((packed)) acpi_fadt_t;

static uint16_t g_pm1a_cnt;
static uint16_t g_pm1b_cnt;
static int g_acpi_pm_ready;

static int acpi_memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *pa = a;
	const unsigned char *pb = b;
	size_t i;

	for (i = 0; i < n; i++)
	{
		if (pa[i] != pb[i])
			return (int)pa[i] - (int)pb[i];
	}
	return 0;
}

static int acpi_checksum_ok(const void *p, size_t n)
{
	const uint8_t *b = p;
	uint8_t sum = 0;
	size_t i;

	for (i = 0; i < n; i++)
		sum += b[i];
	return sum == 0;
}

static int phys_in_early_map(uint64_t p, size_t need)
{
	if (p < 0x1000ull)
		return 0;
	if (p + (uint64_t)need < p)
		return 0;
	if (p + (uint64_t)need > ACPI_SAFE_PHYS_MAX)
		return 0;
	return 1;
}

/*
 * Map [phys, phys+need) identity into the *current* CR3 (kernel or process).
 * Used only for ACPI SDT windows above the early 48MiB identity map — does
 * not expand boot hugepage identity.
 */
static int acpi_map_phys_window(uint64_t phys, size_t need)
{
	uint64_t cr3;
	uint64_t *pml4;
	uint64_t start;
	uint64_t end;
	uint64_t va;

	if (need == 0 || need > ACPI_MAP_MAX_BYTES)
		return -1;
	if (phys < 0x1000ull || phys + (uint64_t)need < phys)
		return -1;
	if (phys + (uint64_t)need > ACPI_MAP_PHYS_CAP)
		return -1;

	__asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
	pml4 = (uint64_t *)(uintptr_t)(cr3 & ~0xFFFULL);
	if (!pml4)
		return -1;

	start = phys & ~0xFFFULL;
	end = (phys + (uint64_t)need + 0xFFFULL) & ~0xFFFULL;
	for (va = start; va < end; va += 0x1000ull)
	{
		if (map_page_in_directory(pml4, va, va, PAGE_PRESENT | PAGE_RW) != 0)
			return -1;
	}
	return 0;
}

static int phys_safe(uint64_t p, size_t need)
{
	if (phys_in_early_map(p, need))
		return 1;
	if (acpi_map_phys_window(p, need) == 0)
		return 1;
	return 0;
}

static void outw_port(uint16_t port, uint16_t val)
{
	__asm__ __volatile__("outw %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

static int fadt_accept(uint64_t phys)
{
	acpi_fadt_t *fadt;

	if (!phys_safe(phys, sizeof(*fadt)))
		return -1;
	fadt = (acpi_fadt_t *)(uintptr_t)phys;
	if (acpi_memcmp(fadt->header.signature, "FACP", 4) != 0)
		return -1;
	if (fadt->header.length < sizeof(*fadt))
		return -1;
	if (!phys_safe(phys, fadt->header.length))
		return -1;
	if (!acpi_checksum_ok(fadt, fadt->header.length) &&
	    (fadt->pm1a_cnt_blk < 0x400u || fadt->pm1a_cnt_blk > 0xFFFu))
		return -1;
	if (fadt->pm1a_cnt_blk == 0 || fadt->pm1a_cnt_blk > 0xFFFFu)
		return -1;

	g_pm1a_cnt = (uint16_t)fadt->pm1a_cnt_blk;
	g_pm1b_cnt = 0;
	if (fadt->pm1b_cnt_blk != 0 && fadt->pm1b_cnt_blk <= 0xFFFFu)
		g_pm1b_cnt = (uint16_t)fadt->pm1b_cnt_blk;
	g_acpi_pm_ready = 1;
	if (phys >= ACPI_SAFE_PHYS_MAX)
		serial_print("ACPI_FADT_MAPPED\n");
	return 0;
}

static int scan_rsdt_for_fadt(uint32_t rsdt_phys)
{
	acpi_sdt_header_t *rsdt;
	uint32_t *ptrs;
	int entries;
	int i;

	if (!phys_safe(rsdt_phys, sizeof(*rsdt)))
		return -1;
	rsdt = (acpi_sdt_header_t *)(uintptr_t)rsdt_phys;
	if (acpi_memcmp(rsdt->signature, "RSDT", 4) != 0)
		return -1;
	if (rsdt->length < sizeof(*rsdt) + 4)
		return -1;
	if (!phys_safe(rsdt_phys, rsdt->length))
		return -1;
	entries = (int)((rsdt->length - sizeof(*rsdt)) / 4);
	if (entries <= 0 || entries > 64)
		return -1;
	ptrs = (uint32_t *)((uint8_t *)rsdt + sizeof(*rsdt));
	for (i = 0; i < entries; i++)
	{
		if (fadt_accept(ptrs[i]) == 0)
			return 0;
	}
	return -1;
}

static int scan_xsdt_for_fadt(uint64_t xsdt_phys)
{
	acpi_sdt_header_t *xsdt;
	uint64_t *ptrs;
	int entries;
	int i;

	if (!phys_safe(xsdt_phys, sizeof(*xsdt)))
		return -1;
	xsdt = (acpi_sdt_header_t *)(uintptr_t)xsdt_phys;
	if (acpi_memcmp(xsdt->signature, "XSDT", 4) != 0)
		return -1;
	if (xsdt->length < sizeof(*xsdt) + 8)
		return -1;
	if (!phys_safe(xsdt_phys, xsdt->length))
		return -1;
	entries = (int)((xsdt->length - sizeof(*xsdt)) / 8);
	if (entries <= 0 || entries > 64)
		return -1;
	ptrs = (uint64_t *)((uint8_t *)xsdt + sizeof(*xsdt));
	for (i = 0; i < entries; i++)
	{
		if (fadt_accept(ptrs[i]) == 0)
			return 0;
	}
	return -1;
}

int ir0_acpi_pm_init(void)
{
	uint32_t addr;

	if (g_acpi_pm_ready)
		return 0;

	for (addr = ACPI_RSDP_SEARCH_START; addr < ACPI_RSDP_SEARCH_END; addr += 16)
	{
		acpi_rsdp_ext_t *rsdp = (acpi_rsdp_ext_t *)(uintptr_t)addr;

		if (acpi_memcmp(rsdp->signature, ACPI_RSDP_SIG, 8) != 0)
			continue;
		if (!acpi_checksum_ok(rsdp, 20))
			continue;

		if (rsdp->revision >= 2 && rsdp->length >= 33 &&
		    rsdp->length <= sizeof(*rsdp) &&
		    acpi_checksum_ok(rsdp, rsdp->length) &&
		    scan_xsdt_for_fadt(rsdp->xsdt_address) == 0)
			return 0;

		if (scan_rsdt_for_fadt(rsdp->rsdt_address) == 0)
			return 0;
	}
	return -1;
}

int ir0_acpi_pm_try_poweroff(void)
{
	uint16_t val = (uint16_t)(0u | ACPI_SLP_EN);

	(void)ir0_acpi_pm_init();
	if (g_acpi_pm_ready && g_pm1a_cnt != 0)
	{
		serial_print("ACPI_PM1A_POWEROFF\n");
		outw_port(g_pm1a_cnt, val);
		if (g_pm1b_cnt)
			outw_port(g_pm1b_cnt, val);
		return 0;
	}

	/* QEMU/Bochs well-known PM1a soft-off ports (no AML). */
	serial_print("ACPI_PM1A_FALLBACK_604\n");
	outw_port(0x604, val);
	outw_port(0xB004, val);
	return 0;
}
