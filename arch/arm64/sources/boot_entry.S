/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * ARM64 firmware entry. Preserve the Linux arm64 boot-protocol x0 DTB pointer
 * before establishing the C stack; no compiler-generated prologue is allowed.
 */

	.section .text.boot, "ax"
	.align  2
	.global _start
	.type   _start, %function
_start:
	adrp	x1, arm64_firmware_fdt
	add	x1, x1, :lo12:arm64_firmware_fdt
	str	x0, [x1]

	adrp	x1, boot_stack
	add	x1, x1, :lo12:boot_stack
	add	sp, x1, #1, lsl #12
	b	boot_main
	.size	_start, .-_start
