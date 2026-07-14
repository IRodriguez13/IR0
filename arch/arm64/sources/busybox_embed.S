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
busybox_aarch64_blob:
	.incbin "build/busybox_aarch64"
busybox_aarch64_blob_end:
