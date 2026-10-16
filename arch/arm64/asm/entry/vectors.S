/* SPDX-License-Identifier: GPL-3.0-only */
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

#ifndef ARM64_EL0_USER_PAGE_PA
#define ARM64_EL0_USER_PAGE_PA 0x42000000
#endif

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

	/*
	 * clone(CLONE_VM|CLONE_VFORK|SIGCHLD, stack=NULL) — Linux aarch64
	 * syscall 220. Child getpid/exit; parent wait4.
	 */
	mov	x0, #0x4111
	mov	x1, xzr
	mov	x8, #220
	svc	#0
	cbnz	x0, 2f
	mov	x8, #172
	svc	#0
	mov	x0, #42
	mov	x8, #93
	svc	#0
2:
	mov	x0, #-1
	movz	x1, #((ARM64_EL0_USER_PAGE_PA + 0x70) & 0xffff)
	movk	x1, #(((ARM64_EL0_USER_PAGE_PA + 0x70) >> 16) & 0xffff), lsl #16
	mov	x2, xzr
	mov	x3, xzr
	mov	x8, #260
	svc	#0

	/*
	 * pipe2 + clone(CLONE_VM|SIGCHLD) without VFORK: child blocking
	 * read, parent write. Proves the 2-task yield (pipe(7)).
	 */
	movz	x0, #((ARM64_EL0_USER_PAGE_PA + 0x80) & 0xffff)
	movk	x0, #(((ARM64_EL0_USER_PAGE_PA + 0x80) >> 16) & 0xffff), lsl #16
	mov	x1, xzr
	mov	x8, #59
	svc	#0
	mov	x0, #0x111
	movz	x1, #((ARM64_EL0_USER_PAGE_PA + 0x400) & 0xffff)
	movk	x1, #(((ARM64_EL0_USER_PAGE_PA + 0x400) >> 16) & 0xffff), lsl #16
	mov	x8, #220
	svc	#0
	cbnz	x0, 3f
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x80) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x80) >> 16) & 0xffff), lsl #16
	ldr	w0, [x9]
	movz	x1, #((ARM64_EL0_USER_PAGE_PA + 0x8c) & 0xffff)
	movk	x1, #(((ARM64_EL0_USER_PAGE_PA + 0x8c) >> 16) & 0xffff), lsl #16
	mov	x2, #1
	mov	x8, #63
	svc	#0
	mov	x0, #0
	mov	x8, #93
	svc	#0
3:
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x80) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x80) >> 16) & 0xffff), lsl #16
	ldr	w0, [x9, #4]
	movz	x1, #((ARM64_EL0_USER_PAGE_PA + 0x8c) & 0xffff)
	movk	x1, #(((ARM64_EL0_USER_PAGE_PA + 0x8c) >> 16) & 0xffff), lsl #16
	mov	w10, #'B'
	strb	w10, [x1]
	mov	x2, #1
	mov	x8, #64
	svc	#0
	mov	x0, #-1
	movz	x1, #((ARM64_EL0_USER_PAGE_PA + 0x70) & 0xffff)
	movk	x1, #(((ARM64_EL0_USER_PAGE_PA + 0x70) >> 16) & 0xffff), lsl #16
	mov	x2, xzr
	mov	x3, xzr
	mov	x8, #260
	svc	#0

	/* nanosleep({0, 1000000}, NULL) — timespec64 at user page +0x40 */
	movz	x0, #((ARM64_EL0_USER_PAGE_PA + 0x40) & 0xffff)
	movk	x0, #(((ARM64_EL0_USER_PAGE_PA + 0x40) >> 16) & 0xffff), lsl #16
	mov	x1, xzr			/* rem = NULL */
	mov	x8, #101
	svc	#0

	/* clock_gettime(CLOCK_MONOTONIC, user_page+0x50) */
	mov	x0, #1			/* CLOCK_MONOTONIC */
	movz	x1, #((ARM64_EL0_USER_PAGE_PA + 0x50) & 0xffff)
	movk	x1, #(((ARM64_EL0_USER_PAGE_PA + 0x50) >> 16) & 0xffff), lsl #16
	mov	x8, #113
	svc	#0

	/* gettimeofday(user_page+0x60, NULL) */
	movz	x0, #((ARM64_EL0_USER_PAGE_PA + 0x60) & 0xffff)
	movk	x0, #(((ARM64_EL0_USER_PAGE_PA + 0x60) >> 16) & 0xffff), lsl #16
	mov	x1, xzr
	mov	x8, #169
	svc	#0

	/* clock_nanosleep(MONOTONIC, 0, timespec@+0x40, NULL) */
	mov	x0, #1
	mov	x1, xzr
	movz	x2, #((ARM64_EL0_USER_PAGE_PA + 0x40) & 0xffff)
	movk	x2, #(((ARM64_EL0_USER_PAGE_PA + 0x40) >> 16) & 0xffff), lsl #16
	mov	x3, xzr
	mov	x8, #115
	svc	#0

	/*
	 * memfd + pwrite64/pread64 + fallocate + statx(AT_EMPTY_PATH) +
	 * msync. Linux aarch64 279/68/67/47/291/227.
	 */
	movz	x0, #((ARM64_EL0_USER_PAGE_PA + 0x90) & 0xffff)
	movk	x0, #(((ARM64_EL0_USER_PAGE_PA + 0x90) >> 16) & 0xffff), lsl #16
	mov	w10, #'m'
	strb	w10, [x0]
	strb	wzr, [x0, #1]
	mov	x1, xzr
	mov	x8, #279
	svc	#0
	mov	x10, x0
	movz	x1, #((ARM64_EL0_USER_PAGE_PA + 0x98) & 0xffff)
	movk	x1, #(((ARM64_EL0_USER_PAGE_PA + 0x98) >> 16) & 0xffff), lsl #16
	mov	w11, #'I'
	strb	w11, [x1]
	mov	x2, #1
	mov	x3, #8
	mov	x8, #68
	svc	#0
	mov	x0, x10
	movz	x1, #((ARM64_EL0_USER_PAGE_PA + 0x9c) & 0xffff)
	movk	x1, #(((ARM64_EL0_USER_PAGE_PA + 0x9c) >> 16) & 0xffff), lsl #16
	mov	x2, #1
	mov	x3, #8
	mov	x8, #67
	svc	#0
	mov	x0, x10
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, #64
	mov	x8, #47
	svc	#0
	mov	x20, x10
	mov	x0, x10
	movz	x1, #((ARM64_EL0_USER_PAGE_PA + 0xb8) & 0xffff)
	movk	x1, #(((ARM64_EL0_USER_PAGE_PA + 0xb8) >> 16) & 0xffff), lsl #16
	strb	wzr, [x1]
	mov	x2, #0x1000
	mov	x3, #0x7ff
	movz	x4, #((ARM64_EL0_USER_PAGE_PA + 0xc0) & 0xffff)
	movk	x4, #(((ARM64_EL0_USER_PAGE_PA + 0xc0) >> 16) & 0xffff), lsl #16
	mov	x8, #291
	svc	#0
	movz	x0, #(ARM64_EL0_USER_PAGE_PA & 0xffff)
	movk	x0, #((ARM64_EL0_USER_PAGE_PA >> 16) & 0xffff), lsl #16
	mov	x1, #4096
	mov	x2, #4
	mov	x8, #227
	svc	#0

	/* preadv(memfd, iov@+0x1c0, 1, 8) */
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x1c0) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x1c0) >> 16) & 0xffff), lsl #16
	movz	x10, #((ARM64_EL0_USER_PAGE_PA + 0x9c) & 0xffff)
	movk	x10, #(((ARM64_EL0_USER_PAGE_PA + 0x9c) >> 16) & 0xffff), lsl #16
	str	x10, [x9]
	mov	x11, #1
	str	x11, [x9, #8]
	mov	x0, x20
	mov	x1, x9
	mov	x2, #1
	mov	x3, #8
	mov	x8, #69
	svc	#0

	/* socketpair + sendmsg/recvmsg SCM_RIGHTS (memfd) */
	mov	x0, #1
	mov	x1, #1
	mov	x2, xzr
	movz	x3, #((ARM64_EL0_USER_PAGE_PA + 0x1d0) & 0xffff)
	movk	x3, #(((ARM64_EL0_USER_PAGE_PA + 0x1d0) >> 16) & 0xffff), lsl #16
	mov	x8, #199
	svc	#0
	movz	x19, #((ARM64_EL0_USER_PAGE_PA + 0x1d0) & 0xffff)
	movk	x19, #(((ARM64_EL0_USER_PAGE_PA + 0x1d0) >> 16) & 0xffff), lsl #16
	ldr	w21, [x19]
	ldr	w22, [x19, #4]
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x220) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x220) >> 16) & 0xffff), lsl #16
	movz	x10, #((ARM64_EL0_USER_PAGE_PA + 0x230) & 0xffff)
	movk	x10, #(((ARM64_EL0_USER_PAGE_PA + 0x230) >> 16) & 0xffff), lsl #16
	mov	w11, #'R'
	strb	w11, [x10]
	str	x10, [x9]
	mov	x11, #1
	str	x11, [x9, #8]
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x238) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x238) >> 16) & 0xffff), lsl #16
	mov	x10, #20
	str	x10, [x9]
	mov	w10, #1
	str	w10, [x9, #8]
	str	w10, [x9, #12]
	str	w20, [x9, #16]
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x1e0) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x1e0) >> 16) & 0xffff), lsl #16
	str	xzr, [x9]
	str	xzr, [x9, #8]
	movz	x10, #((ARM64_EL0_USER_PAGE_PA + 0x220) & 0xffff)
	movk	x10, #(((ARM64_EL0_USER_PAGE_PA + 0x220) >> 16) & 0xffff), lsl #16
	str	x10, [x9, #16]
	mov	x10, #1
	str	x10, [x9, #24]
	movz	x10, #((ARM64_EL0_USER_PAGE_PA + 0x238) & 0xffff)
	movk	x10, #(((ARM64_EL0_USER_PAGE_PA + 0x238) >> 16) & 0xffff), lsl #16
	str	x10, [x9, #32]
	mov	x10, #20
	str	x10, [x9, #40]
	str	xzr, [x9, #48]
	mov	w0, w21
	mov	x1, x9
	mov	x8, #211
	svc	#0
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x2a0) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x2a0) >> 16) & 0xffff), lsl #16
	movz	x10, #((ARM64_EL0_USER_PAGE_PA + 0x2b0) & 0xffff)
	movk	x10, #(((ARM64_EL0_USER_PAGE_PA + 0x2b0) >> 16) & 0xffff), lsl #16
	str	x10, [x9]
	mov	x11, #1
	str	x11, [x9, #8]
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x260) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x260) >> 16) & 0xffff), lsl #16
	str	xzr, [x9]
	str	xzr, [x9, #8]
	movz	x10, #((ARM64_EL0_USER_PAGE_PA + 0x2a0) & 0xffff)
	movk	x10, #(((ARM64_EL0_USER_PAGE_PA + 0x2a0) >> 16) & 0xffff), lsl #16
	str	x10, [x9, #16]
	mov	x10, #1
	str	x10, [x9, #24]
	movz	x10, #((ARM64_EL0_USER_PAGE_PA + 0x2c0) & 0xffff)
	movk	x10, #(((ARM64_EL0_USER_PAGE_PA + 0x2c0) >> 16) & 0xffff), lsl #16
	str	x10, [x9, #32]
	mov	x10, #32
	str	x10, [x9, #40]
	str	xzr, [x9, #48]
	mov	w0, w22
	mov	x1, x9
	mov	x8, #212
	svc	#0

	/* splice(memfd, &off, pipe_w, NULL, 4, 0) + tee + copy_file_range */
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x2e8) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x2e8) >> 16) & 0xffff), lsl #16
	str	xzr, [x9]
	movz	x10, #((ARM64_EL0_USER_PAGE_PA + 0x80) & 0xffff)
	movk	x10, #(((ARM64_EL0_USER_PAGE_PA + 0x80) >> 16) & 0xffff), lsl #16
	mov	x0, x20
	mov	x1, x9
	ldr	w2, [x10, #4]
	mov	x3, xzr
	mov	x4, #4
	mov	x5, xzr
	mov	x8, #76
	svc	#0
	movz	x0, #((ARM64_EL0_USER_PAGE_PA + 0x2e0) & 0xffff)
	movk	x0, #(((ARM64_EL0_USER_PAGE_PA + 0x2e0) >> 16) & 0xffff), lsl #16
	mov	w11, #-1
	str	w11, [x0]
	str	w11, [x0, #4]
	mov	x1, xzr
	mov	x8, #59
	svc	#0
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x80) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x80) >> 16) & 0xffff), lsl #16
	ldr	w0, [x9]
	movz	x10, #((ARM64_EL0_USER_PAGE_PA + 0x2e0) & 0xffff)
	movk	x10, #(((ARM64_EL0_USER_PAGE_PA + 0x2e0) >> 16) & 0xffff), lsl #16
	ldr	w1, [x10, #4]
	mov	x2, #4
	mov	x3, xzr
	mov	x8, #77
	svc	#0
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x2f0) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x2f0) >> 16) & 0xffff), lsl #16
	mov	x10, #48
	str	x10, [x9]
	movz	x11, #((ARM64_EL0_USER_PAGE_PA + 0x2e0) & 0xffff)
	movk	x11, #(((ARM64_EL0_USER_PAGE_PA + 0x2e0) >> 16) & 0xffff), lsl #16
	ldr	w0, [x11]
	mov	x1, xzr
	mov	x2, x20
	mov	x3, x9
	mov	x4, #4
	mov	x5, xzr
	mov	x8, #76
	svc	#0
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x2e8) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x2e8) >> 16) & 0xffff), lsl #16
	str	xzr, [x9]
	movz	x10, #((ARM64_EL0_USER_PAGE_PA + 0x2f0) & 0xffff)
	movk	x10, #(((ARM64_EL0_USER_PAGE_PA + 0x2f0) >> 16) & 0xffff), lsl #16
	mov	x11, #64
	str	x11, [x10]
	mov	x0, x20
	mov	x1, x9
	mov	x2, x20
	mov	x3, x10
	mov	x4, #4
	mov	x5, xzr
	mov	x8, #285
	svc	#0
	mov	x0, x20
	mov	x1, xzr
	mov	x2, #64
	mov	x3, xzr
	mov	x8, #223
	svc	#0
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x2e0) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x2e0) >> 16) & 0xffff), lsl #16
	ldr	w0, [x9]
	ldr	w1, [x9, #4]
	mov	x2, xzr
	mov	x8, #436
	svc	#0

	/* write(1, user probe page, 15) — "ARM64_WRITE_OK\n" filled by boot */
	mov	x0, #1
	movz	x1, #(ARM64_EL0_USER_PAGE_PA & 0xffff)
	movk	x1, #((ARM64_EL0_USER_PAGE_PA >> 16) & 0xffff), lsl #16
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
