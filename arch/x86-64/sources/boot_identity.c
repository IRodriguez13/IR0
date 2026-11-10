/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_identity.c
 * Description: x86-64 boot page-table coverage: which physical ranges
 *              boot_x64.asm already identity-maps for the kernel.
 */

#include <ir0/arch_mm.h>

/* boot_x64.asm identity-maps the low 32 MiB with 2 MiB pages. */
#define X86_BOOT_LOW_IDENTITY_END 0x2000000ULL

/*
 * GRUB framebuffer window: two 2 MiB pages at 0xFD000000 (pd_fb in
 * arch/x86-64/asm/boot/boot_x64.asm).
 */
#define X86_BOOT_FB_WINDOW_START 0xFD000000ULL
#define X86_BOOT_FB_WINDOW_END   0xFD400000ULL

int mm_boot_identity_covers(uint64_t phys, uint64_t size)
{
	uint64_t end = phys + size;

	if (end < phys)
		return 0;
	/*
	 * Only the start is checked against the low map, as the framebuffer
	 * code always did: a range that starts below 32 MiB and runs past it
	 * is reported covered.
	 */
	if (phys < X86_BOOT_LOW_IDENTITY_END)
		return 1;
	return phys >= X86_BOOT_FB_WINDOW_START &&
	       end <= X86_BOOT_FB_WINDOW_END;
}
