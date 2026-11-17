/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Minimal Linux-compatible vDSO exporting __vdso_clock_gettime (version
 * LINUX_2.6, vdso(7)). Delegates to syscall 228 (clock_gettime).
 */

	.section .note.GNU-stack,"",%progbits
	.text
	.global __vdso_clock_gettime
	.type __vdso_clock_gettime, @function
	.align 16
__vdso_clock_gettime:
	mov $228, %eax
	syscall
	ret
	.size __vdso_clock_gettime, . - __vdso_clock_gettime
