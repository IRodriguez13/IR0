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

	/* getcwd(2) — Linux aarch64 syscall 17 */
	movz	x0, #((ARM64_EL0_USER_PAGE_PA + 0x300) & 0xffff)
	movk	x0, #(((ARM64_EL0_USER_PAGE_PA + 0x300) >> 16) & 0xffff), lsl #16
	mov	x1, #256
	mov	x8, #17
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

	/* vmsplice + sync_file_range + abstract AF_UNIX + sendmmsg */
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x300) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x300) >> 16) & 0xffff), lsl #16
	mov	w10, #'V'
	strb	w10, [x9]
	movz	x10, #((ARM64_EL0_USER_PAGE_PA + 0x308) & 0xffff)
	movk	x10, #(((ARM64_EL0_USER_PAGE_PA + 0x308) >> 16) & 0xffff), lsl #16
	str	x9, [x10]
	mov	x11, #1
	str	x11, [x10, #8]
	movz	x12, #((ARM64_EL0_USER_PAGE_PA + 0x80) & 0xffff)
	movk	x12, #(((ARM64_EL0_USER_PAGE_PA + 0x80) >> 16) & 0xffff), lsl #16
	ldr	w0, [x12, #4]
	mov	x1, x10
	mov	x2, #1
	mov	x3, xzr
	mov	x8, #75
	svc	#0
	mov	x0, x20
	mov	x1, xzr
	mov	x2, #64
	mov	x3, #6
	mov	x8, #84
	svc	#0
	mov	x0, #1
	mov	x1, #2
	mov	x2, xzr
	mov	x8, #198
	svc	#0
	mov	w23, w0
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x318) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x318) >> 16) & 0xffff), lsl #16
	mov	w10, #1
	strb	w10, [x9]
	strb	wzr, [x9, #1]
	strb	wzr, [x9, #2]
	mov	w10, #'A'
	strb	w10, [x9, #3]
	mov	w0, w23
	mov	x1, x9
	mov	x2, #4
	mov	x8, #200
	svc	#0
	mov	x0, #1
	mov	x1, #2
	mov	x2, xzr
	mov	x8, #198
	svc	#0
	mov	w24, w0
	mov	w0, w24
	movz	x1, #((ARM64_EL0_USER_PAGE_PA + 0x300) & 0xffff)
	movk	x1, #(((ARM64_EL0_USER_PAGE_PA + 0x300) >> 16) & 0xffff), lsl #16
	mov	x2, #1
	mov	x3, xzr
	movz	x4, #((ARM64_EL0_USER_PAGE_PA + 0x318) & 0xffff)
	movk	x4, #(((ARM64_EL0_USER_PAGE_PA + 0x318) >> 16) & 0xffff), lsl #16
	mov	x5, #4
	mov	x8, #206
	svc	#0
	mov	w0, w23
	movz	x1, #((ARM64_EL0_USER_PAGE_PA + 0x348) & 0xffff)
	movk	x1, #(((ARM64_EL0_USER_PAGE_PA + 0x348) >> 16) & 0xffff), lsl #16
	mov	x2, #1
	mov	x8, #207
	svc	#0
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x34c) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x34c) >> 16) & 0xffff), lsl #16
	mov	w10, #1
	str	w10, [x9]
	mov	w0, w23
	mov	x1, #1
	mov	x2, #16
	mov	x3, x9
	mov	x4, #4
	mov	x8, #208
	svc	#0
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x360) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x360) >> 16) & 0xffff), lsl #16
	movz	x10, #((ARM64_EL0_USER_PAGE_PA + 0x308) & 0xffff)
	movk	x10, #(((ARM64_EL0_USER_PAGE_PA + 0x308) >> 16) & 0xffff), lsl #16
	str	xzr, [x9]
	str	xzr, [x9, #8]
	str	x10, [x9, #16]
	mov	x10, #1
	str	x10, [x9, #24]
	str	xzr, [x9, #32]
	str	xzr, [x9, #40]
	str	xzr, [x9, #48]
	str	xzr, [x9, #56]
	mov	w0, w24
	mov	x1, x9
	mov	x2, #1
	mov	x3, xzr
	mov	x8, #269
	svc	#0

	/*
	 * prctl name, getcpu, affinity, mincore, mlock, readahead,
	 * syncfs/fsync, membarrier, process_vm_readv.
	 * Linux aarch64 167/168/122/123/232/228/213/267/81/283/270.
	 */
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0xc0) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0xc0) >> 16) & 0xffff), lsl #16
	mov	w10, #'p'
	strb	w10, [x9]
	mov	w10, #'r'
	strb	w10, [x9, #1]
	mov	w10, #'o'
	strb	w10, [x9, #2]
	mov	w10, #'b'
	strb	w10, [x9, #3]
	mov	w10, #'e'
	strb	w10, [x9, #4]
	strb	wzr, [x9, #5]
	mov	x0, #15
	mov	x1, x9
	mov	x8, #167
	svc	#0
	mov	x0, #16
	movz	x1, #((ARM64_EL0_USER_PAGE_PA + 0xd0) & 0xffff)
	movk	x1, #(((ARM64_EL0_USER_PAGE_PA + 0xd0) >> 16) & 0xffff), lsl #16
	mov	x8, #167
	svc	#0
	movz	x0, #((ARM64_EL0_USER_PAGE_PA + 0x50) & 0xffff)
	movk	x0, #(((ARM64_EL0_USER_PAGE_PA + 0x50) >> 16) & 0xffff), lsl #16
	movz	x1, #((ARM64_EL0_USER_PAGE_PA + 0x54) & 0xffff)
	movk	x1, #(((ARM64_EL0_USER_PAGE_PA + 0x54) >> 16) & 0xffff), lsl #16
	mov	x2, xzr
	mov	x8, #168
	svc	#0
	mov	x0, xzr
	mov	x1, #8
	movz	x2, #((ARM64_EL0_USER_PAGE_PA + 0xe0) & 0xffff)
	movk	x2, #(((ARM64_EL0_USER_PAGE_PA + 0xe0) >> 16) & 0xffff), lsl #16
	mov	x8, #123
	svc	#0
	mov	x0, xzr
	mov	x1, #8
	movz	x2, #((ARM64_EL0_USER_PAGE_PA + 0xe0) & 0xffff)
	movk	x2, #(((ARM64_EL0_USER_PAGE_PA + 0xe0) >> 16) & 0xffff), lsl #16
	mov	x8, #122
	svc	#0
	mov	x0, xzr
	mov	x8, #120
	svc	#0
	mov	x0, xzr
	movz	x1, #((ARM64_EL0_USER_PAGE_PA + 0xe8) & 0xffff)
	movk	x1, #(((ARM64_EL0_USER_PAGE_PA + 0xe8) >> 16) & 0xffff), lsl #16
	mov	x8, #121
	svc	#0
	movz	x0, #(ARM64_EL0_USER_PAGE_PA & 0xffff)
	movk	x0, #((ARM64_EL0_USER_PAGE_PA >> 16) & 0xffff), lsl #16
	mov	x1, #4096
	movz	x2, #((ARM64_EL0_USER_PAGE_PA + 0xec) & 0xffff)
	movk	x2, #(((ARM64_EL0_USER_PAGE_PA + 0xec) >> 16) & 0xffff), lsl #16
	mov	x8, #232
	svc	#0
	movz	x0, #(ARM64_EL0_USER_PAGE_PA & 0xffff)
	movk	x0, #((ARM64_EL0_USER_PAGE_PA >> 16) & 0xffff), lsl #16
	mov	x1, #4096
	mov	x8, #228
	svc	#0
	movz	x0, #(ARM64_EL0_USER_PAGE_PA & 0xffff)
	movk	x0, #((ARM64_EL0_USER_PAGE_PA >> 16) & 0xffff), lsl #16
	mov	x1, #4096
	mov	x8, #229
	svc	#0
	mov	x0, #1
	mov	x8, #230
	svc	#0
	mov	x8, #231
	svc	#0
	mov	x0, x20
	mov	x1, xzr
	mov	x2, #64
	mov	x8, #213
	svc	#0
	mov	x0, #1
	mov	x8, #267
	svc	#0
	mov	x0, x20
	mov	x8, #82
	svc	#0
	mov	x0, xzr
	mov	x1, xzr
	mov	x8, #283
	svc	#0
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x3b0) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x3b0) >> 16) & 0xffff), lsl #16
	movz	x10, #((ARM64_EL0_USER_PAGE_PA + 0x3a0) & 0xffff)
	movk	x10, #(((ARM64_EL0_USER_PAGE_PA + 0x3a0) >> 16) & 0xffff), lsl #16
	str	x10, [x9]
	mov	x11, #1
	str	x11, [x9, #8]
	movz	x12, #((ARM64_EL0_USER_PAGE_PA + 0x3c0) & 0xffff)
	movk	x12, #(((ARM64_EL0_USER_PAGE_PA + 0x3c0) >> 16) & 0xffff), lsl #16
	movz	x13, #((ARM64_EL0_USER_PAGE_PA + 0x8c) & 0xffff)
	movk	x13, #(((ARM64_EL0_USER_PAGE_PA + 0x8c) >> 16) & 0xffff), lsl #16
	str	x13, [x12]
	str	x11, [x12, #8]
	mov	x0, xzr
	mov	x1, x9
	mov	x2, #1
	mov	x3, x12
	mov	x4, #1
	mov	x5, xzr
	mov	x8, #270
	svc	#0

	/*
	 * times, capget, sched priority, ioprio, kcmp, pidfd, faccessat2,
	 * preadv2. Linux aarch64 153/90/125/31/272/434/424/439/286.
	 */
	movz	x0, #((ARM64_EL0_USER_PAGE_PA + 0x40) & 0xffff)
	movk	x0, #(((ARM64_EL0_USER_PAGE_PA + 0x40) >> 16) & 0xffff), lsl #16
	mov	x8, #153
	svc	#0
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0xc0) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0xc0) >> 16) & 0xffff), lsl #16
	movz	w10, #0x0522
	movk	w10, #0x2008, lsl #16
	str	w10, [x9]
	str	wzr, [x9, #4]
	mov	x0, x9
	add	x1, x9, #8
	mov	x8, #90
	svc	#0
	mov	x0, xzr
	mov	x8, #125
	svc	#0
	mov	x0, #1
	mov	x1, xzr
	mov	x8, #31
	svc	#0
	mov	x0, #1
	mov	x1, #1
	mov	x2, #1
	mov	x8, #272
	svc	#0
	mov	x0, #1
	mov	x1, xzr
	mov	x8, #434
	svc	#0
	mov	x21, x0
	mov	x0, x21
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	mov	x8, #424
	svc	#0
	mov	x0, x21
	mov	x1, #1
	mov	x2, xzr
	mov	x8, #438
	svc	#0
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x3d0) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x3d0) >> 16) & 0xffff), lsl #16
	mov	w10, #'/'
	strb	w10, [x9]
	strb	wzr, [x9, #1]
	mov	x0, #-100
	mov	x1, x9
	mov	x2, xzr
	mov	x3, xzr
	mov	x8, #439
	svc	#0
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
	mov	x4, xzr
	mov	x5, xzr
	mov	x8, #286
	svc	#0

	/*
	 * setfsuid/setfsgid, settimeofday, clock_adjtime, fsetxattr/fgetxattr,
	 * name_to_handle_at/open_by_handle_at, fchmodat2, epoll_pwait2,
	 * process_madvise, clone3(CLONE_VM|CLONE_VFORK). Linux aarch64
	 * 151/152/170/266/7/10/264/265/452/441/440/435.
	 */
	mov	x0, xzr
	mov	x8, #151
	svc	#0
	mov	x0, xzr
	mov	x8, #152
	svc	#0
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x60) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x60) >> 16) & 0xffff), lsl #16
	str	xzr, [x9]
	str	xzr, [x9, #8]
	mov	x0, x9
	mov	x1, xzr
	mov	x8, #170
	svc	#0
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x100) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x100) >> 16) & 0xffff), lsl #16
	str	xzr, [x9]
	str	xzr, [x9, #8]
	mov	x0, xzr
	mov	x1, x9
	mov	x8, #266
	svc	#0
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x180) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x180) >> 16) & 0xffff), lsl #16
	mov	w10, #'u'
	strb	w10, [x9]
	mov	w10, #'s'
	strb	w10, [x9, #1]
	mov	w10, #'e'
	strb	w10, [x9, #2]
	mov	w10, #'r'
	strb	w10, [x9, #3]
	mov	w10, #'.'
	strb	w10, [x9, #4]
	mov	w10, #'t'
	strb	w10, [x9, #5]
	strb	wzr, [x9, #6]
	movz	x11, #((ARM64_EL0_USER_PAGE_PA + 0x190) & 0xffff)
	movk	x11, #(((ARM64_EL0_USER_PAGE_PA + 0x190) >> 16) & 0xffff), lsl #16
	mov	w10, #'X'
	strb	w10, [x11]
	mov	x0, x20
	mov	x1, x9
	mov	x2, x11
	mov	x3, #1
	mov	x4, xzr
	mov	x8, #7
	svc	#0
	mov	x0, x20
	mov	x1, x9
	mov	x2, x11
	mov	x3, #1
	mov	x8, #10
	svc	#0
	movz	x12, #((ARM64_EL0_USER_PAGE_PA + 0x1a0) & 0xffff)
	movk	x12, #(((ARM64_EL0_USER_PAGE_PA + 0x1a0) >> 16) & 0xffff), lsl #16
	mov	w10, #8
	str	w10, [x12]
	movz	x13, #((ARM64_EL0_USER_PAGE_PA + 0xb8) & 0xffff)
	movk	x13, #(((ARM64_EL0_USER_PAGE_PA + 0xb8) >> 16) & 0xffff), lsl #16
	strb	wzr, [x13]
	mov	x0, x20
	mov	x1, x13
	mov	x2, x12
	movz	x3, #((ARM64_EL0_USER_PAGE_PA + 0x1b0) & 0xffff)
	movk	x3, #(((ARM64_EL0_USER_PAGE_PA + 0x1b0) >> 16) & 0xffff), lsl #16
	mov	x4, #0x1000
	mov	x8, #264
	svc	#0
	mov	x0, #-100
	mov	x1, x12
	mov	x2, #2
	mov	x8, #265
	svc	#0
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x3d0) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x3d0) >> 16) & 0xffff), lsl #16
	mov	x0, #-100
	mov	x1, x9
	mov	x2, #0644
	mov	x3, xzr
	mov	x8, #452
	svc	#0
	mov	x0, xzr
	mov	x8, #20
	svc	#0
	mov	w25, w0
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x50) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x50) >> 16) & 0xffff), lsl #16
	str	xzr, [x9]
	str	xzr, [x9, #8]
	mov	w0, w25
	movz	x1, #((ARM64_EL0_USER_PAGE_PA + 0x1c0) & 0xffff)
	movk	x1, #(((ARM64_EL0_USER_PAGE_PA + 0x1c0) >> 16) & 0xffff), lsl #16
	mov	x2, #1
	mov	x3, x9
	mov	x4, xzr
	mov	x5, xzr
	mov	x8, #441
	svc	#0
	mov	x0, x21
	movz	x1, #((ARM64_EL0_USER_PAGE_PA + 0x3b0) & 0xffff)
	movk	x1, #(((ARM64_EL0_USER_PAGE_PA + 0x3b0) >> 16) & 0xffff), lsl #16
	mov	x2, #1
	mov	x3, #4
	mov	x4, xzr
	mov	x8, #440
	svc	#0
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x100) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x100) >> 16) & 0xffff), lsl #16
	mov	x10, #8
2:
	str	xzr, [x9], #8
	subs	x10, x10, #1
	b.ne	2b
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x100) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x100) >> 16) & 0xffff), lsl #16
	mov	x10, #0x4100
	str	x10, [x9]
	mov	x10, #17
	str	x10, [x9, #32]
	movz	x10, #((ARM64_EL0_USER_PAGE_PA + 0x400) & 0xffff)
	movk	x10, #(((ARM64_EL0_USER_PAGE_PA + 0x400) >> 16) & 0xffff), lsl #16
	str	x10, [x9, #40]
	mov	x10, #0x200
	str	x10, [x9, #48]
	mov	x0, x9
	mov	x1, #64
	mov	x8, #435
	svc	#0
	cbnz	x0, 4f
	mov	x0, xzr
	mov	x8, #93
	svc	#0
4:
	mov	x0, #-1
	movz	x1, #((ARM64_EL0_USER_PAGE_PA + 0x70) & 0xffff)
	movk	x1, #(((ARM64_EL0_USER_PAGE_PA + 0x70) >> 16) & 0xffff), lsl #16
	mov	x2, xzr
	mov	x3, xzr
	mov	x8, #260
	svc	#0

	/*
	 * cachestat, mseal, futex_waitv/wake/wait/requeue, setxattrat,
	 * file_getattr/setattr. Linux aarch64 451/462/449/454/455/456/463/468/469.
	 */
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x1c0) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x1c0) >> 16) & 0xffff), lsl #16
	str	xzr, [x9]
	str	xzr, [x9, #8]
	movz	x10, #((ARM64_EL0_USER_PAGE_PA + 0x100) & 0xffff)
	movk	x10, #(((ARM64_EL0_USER_PAGE_PA + 0x100) >> 16) & 0xffff), lsl #16
	mov	x0, x20
	mov	x1, x9
	mov	x2, x10
	mov	x3, xzr
	mov	x8, #451
	svc	#0
	movz	x0, #(ARM64_EL0_USER_PAGE_PA & 0xffff)
	movk	x0, #((ARM64_EL0_USER_PAGE_PA >> 16) & 0xffff), lsl #16
	mov	x1, #4096
	mov	x2, xzr
	mov	x8, #462
	svc	#0
	movz	x11, #((ARM64_EL0_USER_PAGE_PA + 0x1b4) & 0xffff)
	movk	x11, #(((ARM64_EL0_USER_PAGE_PA + 0x1b4) >> 16) & 0xffff), lsl #16
	mov	w12, #5
	str	w12, [x11]
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x140) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x140) >> 16) & 0xffff), lsl #16
	str	xzr, [x9]
	str	x11, [x9, #8]
	mov	w12, #2
	str	w12, [x9, #16]
	str	wzr, [x9, #20]
	mov	x0, x9
	mov	x1, #1
	mov	x2, xzr
	mov	x3, xzr
	mov	x4, xzr
	mov	x8, #449
	svc	#0
	mov	x0, x11
	mov	x1, #0xffffffff
	mov	x2, #1
	mov	x3, #2
	mov	x8, #454
	svc	#0
	mov	x0, x11
	mov	x1, xzr
	mov	x2, #0xffffffff
	mov	x3, #2
	mov	x4, xzr
	mov	x5, xzr
	mov	x8, #455
	svc	#0
	str	xzr, [x9, #24]
	str	x11, [x9, #32]
	mov	w12, #2
	str	w12, [x9, #40]
	str	wzr, [x9, #44]
	mov	x0, x9
	mov	x1, xzr
	mov	x2, #1
	mov	x3, #0
	mov	x8, #456
	svc	#0
	movz	x13, #((ARM64_EL0_USER_PAGE_PA + 0xb8) & 0xffff)
	movk	x13, #(((ARM64_EL0_USER_PAGE_PA + 0xb8) >> 16) & 0xffff), lsl #16
	strb	wzr, [x13]
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x180) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x180) >> 16) & 0xffff), lsl #16
	movz	x11, #((ARM64_EL0_USER_PAGE_PA + 0x190) & 0xffff)
	movk	x11, #(((ARM64_EL0_USER_PAGE_PA + 0x190) >> 16) & 0xffff), lsl #16
	movz	x14, #((ARM64_EL0_USER_PAGE_PA + 0x1d0) & 0xffff)
	movk	x14, #(((ARM64_EL0_USER_PAGE_PA + 0x1d0) >> 16) & 0xffff), lsl #16
	str	x11, [x14]
	mov	w12, #1
	str	w12, [x14, #8]
	str	wzr, [x14, #12]
	mov	x0, x20
	mov	x1, x13
	mov	x2, #0x1000
	mov	x3, x9
	mov	x4, x14
	mov	x5, #16
	mov	x8, #463
	svc	#0
	movz	x15, #((ARM64_EL0_USER_PAGE_PA + 0x1e0) & 0xffff)
	movk	x15, #(((ARM64_EL0_USER_PAGE_PA + 0x1e0) >> 16) & 0xffff), lsl #16
	str	xzr, [x15]
	str	xzr, [x15, #8]
	str	xzr, [x15, #16]
	mov	x0, x20
	mov	x1, x13
	mov	x2, x15
	mov	x3, #24
	mov	x4, #0x1000
	mov	x8, #468
	svc	#0
	str	xzr, [x15]
	str	xzr, [x15, #8]
	str	xzr, [x15, #16]
	mov	x0, x20
	mov	x1, x13
	mov	x2, x15
	mov	x3, #24
	mov	x4, #0x1000
	mov	x8, #469
	svc	#0

	/*
	 * landlock ABI 3 + statmount/listmount + lsm_list. Linux aarch64
	 * 444/445/446/457/458/461. Restrict "/" so later write(1) stays open.
	 */
	mov	x0, xzr
	mov	x1, xzr
	mov	x2, #1
	mov	x8, #444
	svc	#0
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x100) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x100) >> 16) & 0xffff), lsl #16
	mov	x12, #0xe
	str	x12, [x9]
	mov	x0, x9
	mov	x1, #8
	mov	x2, xzr
	mov	x8, #444
	svc	#0
	mov	x21, x0
	movz	x13, #((ARM64_EL0_USER_PAGE_PA + 0x3d0) & 0xffff)
	movk	x13, #(((ARM64_EL0_USER_PAGE_PA + 0x3d0) >> 16) & 0xffff), lsl #16
	mov	w12, #'/'
	strb	w12, [x13]
	strb	wzr, [x13, #1]
	mov	x0, #-100
	mov	x1, x13
	mov	x2, xzr
	mov	x8, #56
	svc	#0
	mov	x22, x0
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x140) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x140) >> 16) & 0xffff), lsl #16
	mov	x12, #0xe
	str	x12, [x9]
	str	w22, [x9, #8]
	mov	x0, x21
	mov	x1, #1
	mov	x2, x9
	mov	x3, xzr
	mov	x8, #445
	svc	#0
	mov	x0, x21
	mov	x1, xzr
	mov	x8, #446
	svc	#0
	movz	x14, #((ARM64_EL0_USER_PAGE_PA + 0x1c0) & 0xffff)
	movk	x14, #(((ARM64_EL0_USER_PAGE_PA + 0x1c0) >> 16) & 0xffff), lsl #16
	mov	w12, #24
	str	w12, [x14]
	str	wzr, [x14, #4]
	mov	x12, #-1
	str	x12, [x14, #8]
	str	xzr, [x14, #16]
	movz	x15, #((ARM64_EL0_USER_PAGE_PA + 0x1e0) & 0xffff)
	movk	x15, #(((ARM64_EL0_USER_PAGE_PA + 0x1e0) >> 16) & 0xffff), lsl #16
	mov	x0, x14
	mov	x1, x15
	mov	x2, #1
	mov	x3, xzr
	mov	x8, #458
	svc	#0
	movz	x12, #0x0001
	movk	x12, #0x8000, lsl #16
	str	x12, [x14, #8]
	movz	x16, #((ARM64_EL0_USER_PAGE_PA + 0x200) & 0xffff)
	movk	x16, #(((ARM64_EL0_USER_PAGE_PA + 0x200) >> 16) & 0xffff), lsl #16
	mov	x0, x14
	mov	x1, x16
	mov	x2, #522
	mov	x3, xzr
	mov	x8, #457
	svc	#0
	movz	x17, #((ARM64_EL0_USER_PAGE_PA + 0x50) & 0xffff)
	movk	x17, #(((ARM64_EL0_USER_PAGE_PA + 0x50) >> 16) & 0xffff), lsl #16
	mov	w12, #8
	str	w12, [x17]
	mov	x0, x15
	mov	x1, x17
	mov	x2, xzr
	mov	x8, #461
	svc	#0

	/*
	 * unshare(0), adjtimex(REALTIME), setns(pidfd, NEWNS), process_mrelease.
	 * Linux aarch64 97/171/268/448.
	 */
	mov	x0, xzr
	mov	x8, #97
	svc	#0
	movz	x9, #((ARM64_EL0_USER_PAGE_PA + 0x100) & 0xffff)
	movk	x9, #(((ARM64_EL0_USER_PAGE_PA + 0x100) >> 16) & 0xffff), lsl #16
	str	xzr, [x9]
	str	xzr, [x9, #8]
	mov	x0, x9
	mov	x8, #171
	svc	#0
	mov	x0, #1
	mov	x1, xzr
	mov	x8, #434
	svc	#0
	mov	x21, x0
	mov	x0, x21
	movz	x1, #0x2, lsl #16
	mov	x8, #268
	svc	#0
	mov	x0, x21
	mov	x1, xzr
	mov	x8, #448
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
