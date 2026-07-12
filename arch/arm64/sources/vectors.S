/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vectors.S
 * Description: EL1 exception vector table (VBAR_EL1); F7.2 early bring-up.
 *
 * Layout: 16 entries × 0x80 bytes, table 0x800-aligned (ARM DDI 0487).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

	.section .vectors, "ax"
	.balign 0x800
	.global ir0_el1_vectors
ir0_el1_vectors:

	/* Current EL with SP0 */
	.org 0x000
	b	exc_unhandled
	.org 0x080
	b	exc_unhandled
	.org 0x100
	b	exc_unhandled
	.org 0x180
	b	exc_unhandled

	/* Current EL with SPx (EL1h) — SVC from EL1 lands on sync here */
	.org 0x200
	b	exc_sync_el1h
	.org 0x280
	b	exc_unhandled
	.org 0x300
	b	exc_unhandled
	.org 0x380
	b	exc_unhandled

	/* Lower EL AArch64 */
	.org 0x400
	b	exc_unhandled
	.org 0x480
	b	exc_unhandled
	.org 0x500
	b	exc_unhandled
	.org 0x580
	b	exc_unhandled

	/* Lower EL AArch32 */
	.org 0x600
	b	exc_unhandled
	.org 0x680
	b	exc_unhandled
	.org 0x700
	b	exc_unhandled
	.org 0x780
	b	exc_unhandled

	.org 0x800

	.text
	.align 2
	.global exc_sync_el1h

/*
 * Save volatile regs, call C sync handler, restore, eret.
 * SP is already boot_stack (SP_EL1) from early boot.
 */
exc_sync_el1h:
	sub	sp, sp, #256
	stp	x0, x1, [sp, #0]
	stp	x2, x3, [sp, #16]
	stp	x4, x5, [sp, #32]
	stp	x6, x7, [sp, #48]
	stp	x8, x9, [sp, #64]
	stp	x10, x11, [sp, #80]
	stp	x12, x13, [sp, #96]
	stp	x14, x15, [sp, #112]
	stp	x16, x17, [sp, #128]
	stp	x18, x19, [sp, #144]
	stp	x20, x21, [sp, #160]
	stp	x22, x23, [sp, #176]
	stp	x24, x25, [sp, #192]
	stp	x26, x27, [sp, #208]
	stp	x28, x29, [sp, #224]
	str	x30, [sp, #240]
	bl	arm64_exc_sync_el1
	ldr	x30, [sp, #240]
	ldp	x28, x29, [sp, #224]
	ldp	x26, x27, [sp, #208]
	ldp	x24, x25, [sp, #192]
	ldp	x22, x23, [sp, #176]
	ldp	x20, x21, [sp, #160]
	ldp	x18, x19, [sp, #144]
	ldp	x16, x17, [sp, #128]
	ldp	x14, x15, [sp, #112]
	ldp	x12, x13, [sp, #96]
	ldp	x10, x11, [sp, #80]
	ldp	x8, x9, [sp, #64]
	ldp	x6, x7, [sp, #48]
	ldp	x4, x5, [sp, #32]
	ldp	x2, x3, [sp, #16]
	ldp	x0, x1, [sp, #0]
	add	sp, sp, #256
	eret

exc_unhandled:
	b	exc_unhandled
