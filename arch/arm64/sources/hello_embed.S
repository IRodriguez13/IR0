/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: hello_embed.S
 * Description: Embed build/hello_aarch64 (musl static, non-PIE) as a blob.
 */

	.section .rodata
	.align 3
	.global hello_aarch64_blob
	.global hello_aarch64_blob_end
#ifndef ARM64_HELLO_BLOB_PATH
#define ARM64_HELLO_BLOB_PATH "build/hello_aarch64"
#endif
hello_aarch64_blob:
	.incbin ARM64_HELLO_BLOB_PATH
hello_aarch64_blob_end:
