/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * ARM64 firmware entry. Preserve the Linux arm64 boot-protocol x0 DTB pointer,
 * normalize an EL2 firmware entry to EL1h, then establish the C stack. No
 * compiler-generated prologue is allowed before x0 has been saved.
 */

	.section .text.boot, "ax"
	.align  2
	.global _start
	.type   _start, %function
_start:
	adrp	x1, arm64_firmware_fdt
	add	x1, x1, :lo12:arm64_firmware_fdt
	str	x0, [x1]

	/* The common kernel always starts in EL1, independently of firmware. */
	mrs	x2, CurrentEL
	cmp	x2, #0x4
	b.eq	.Lel1_entry
	cmp	x2, #0x8
	b.ne	.Lunsupported_el

	/* EL1 executes AArch64 and receives the physical timer directly. */
	mov	x3, #0x80000000
	msr	hcr_el2, x3
	mov	x3, #3
	msr	cnthctl_el2, x3
	msr	cntvoff_el2, xzr

	/* Do not inherit firmware traps or stage-2 translation state. */
	msr	cptr_el2, xzr
	msr	hstr_el2, xzr
	msr	vttbr_el2, xzr

	adr	x3, .Lel1_entry
	msr	elr_el2, x3
	mov	x3, #0x3c5		/* DAIF masked, EL1h. */
	msr	spsr_el2, x3
	isb
	eret

.Lel1_entry:
	msr	daifset, #0xf
	adrp	x1, boot_stack
	add	x1, x1, :lo12:boot_stack
	add	sp, x1, #1, lsl #12
	b	boot_main

.Lunsupported_el:
	wfe
	b	.Lunsupported_el
	.size	_start, .-_start
