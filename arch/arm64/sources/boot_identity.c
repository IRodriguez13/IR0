/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_identity.c
 * Description: ARM64 boot page-table coverage for drivers that map
 *              firmware-described memory (framebuffers).
 */

#include <ir0/arch_mm.h>

/*
 * The boot MMU (mmu.c) maps DRAM as Normal memory and device blocks on
 * request; a framebuffer needs its own mapping either way, so nothing is
 * reported as pre-mapped.
 */
int mm_boot_identity_covers(uint64_t phys, uint64_t size)
{
	(void)phys;
	(void)size;
	return 0;
}
