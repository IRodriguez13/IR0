/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Raspberry Pi / board-min images: keep boot_main as the C entry until they
 * share the full virt bring-up path (kmain + portable boot_flow).
 */
	.section .text.boot, "ax"
	.align  3
	.global _start
	.type   _start, %function
_start:
	adrp	x1, arm64_firmware_fdt
	add	x1, x1, :lo12:arm64_firmware_fdt
	str	x0, [x1]
	msr	daifset, #0xf
	adrp	x1, boot_stack
	add	x1, x1, :lo12:boot_stack
	add	sp, x1, #1, lsl #12
	b	boot_main
	.size	_start, .-_start
