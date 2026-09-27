/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: busybox_embed.S
 * Description: Embed build/busybox_aarch64 (static musl BusyBox) as a blob.
 */

	.section .rodata
	.align 3
	.global busybox_aarch64_blob
	.global busybox_aarch64_blob_end
#ifndef ARM64_BUSYBOX_BLOB_PATH
#define ARM64_BUSYBOX_BLOB_PATH "build/busybox_aarch64"
#endif
busybox_aarch64_blob:
	.incbin ARM64_BUSYBOX_BLOB_PATH
busybox_aarch64_blob_end:
