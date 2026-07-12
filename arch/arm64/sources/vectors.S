/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vectors.S
 * Description: EL1 exception vector table (VBAR_EL1); F7.2–F7c early bring-up.
 *
 * Layout: 16 entries × 0x80 bytes, table 0x800-aligned (ARM DDI 0487).
 * Lower-EL sync passes frame pointer so SVC can update x0 (syscall return).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

	.macro	exc_entry, c_handler
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
	bl	\c_handler
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
	.endm

	/* Like exc_entry but passes SP (frame) in x0 to the C handler. */
	.macro	exc_entry_frame, c_handler
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
	mov	x0, sp
	bl	\c_handler
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
	.endm

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

	/* Current EL with SPx (EL1h) */
	.org 0x200
	b	exc_sync_el1h
	.org 0x280
	b	exc_irq_el1h
	.org 0x300
	b	exc_unhandled
	.org 0x380
	b	exc_unhandled

	/* Lower EL AArch64 — EL0 SVC lands on sync here */
	.org 0x400
	b	exc_sync_lower
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
	.global exc_sync_lower
	.global exc_irq_el1h
	.global el0_entry

exc_sync_el1h:
	exc_entry arm64_exc_sync_el1

exc_sync_lower:
	exc_entry_frame arm64_exc_sync_lower

exc_irq_el1h:
	exc_entry arm64_exc_irq_el1

exc_unhandled:
	b	exc_unhandled

/*
 * F7c–F7g EL0 workload (Linux aarch64 ABI: nr=x8, args=x0.., ret=x0):
 *   getpid → nanosleep → clock_gettime → gettimeofday → clock_nanosleep → write → exit.
 */
	.section .text.el0, "ax"
	.align 2
el0_entry:
	/* getpid */
	mov	x8, #172
	svc	#0

	/* nanosleep({0, 1000000}, NULL) — timespec64 at user page +0x40 */
	movz	x0, #0x0040
	movk	x0, #0x4200, lsl #16
	mov	x1, xzr			/* rem = NULL */
	mov	x8, #101
	svc	#0

	/* clock_gettime(CLOCK_MONOTONIC, user_page+0x50) */
	mov	x0, #1			/* CLOCK_MONOTONIC */
	movz	x1, #0x0050
	movk	x1, #0x4200, lsl #16
	mov	x8, #113
	svc	#0

	/* gettimeofday(user_page+0x60, NULL) */
	movz	x0, #0x0060
	movk	x0, #0x4200, lsl #16
	mov	x1, xzr
	mov	x8, #169
	svc	#0

	/* clock_nanosleep(MONOTONIC, 0, timespec@+0x40, NULL) */
	mov	x0, #1
	mov	x1, xzr
	movz	x2, #0x0040
	movk	x2, #0x4200, lsl #16
	mov	x3, xzr
	mov	x8, #115
	svc	#0

	/* write(1, 0x42000000, 15) — "ARM64_WRITE_OK\n" filled by boot */
	mov	x0, #1
	movz	x1, #0x0000
	movk	x1, #0x4200, lsl #16
	mov	x2, #15
	mov	x8, #64
	svc	#0

	/* exit → leave EL0 */
	mov	x0, #0
	mov	x8, #93
	svc	#0

1:
	wfe
	b	1b
