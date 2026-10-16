/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: embedded_elf_embed.S
 * Description: Embed a static ET_EXEC blob for the early EL0 guest.
 */

	.section .rodata
	.align 3
	.global embedded_elf_blob
	.global embedded_elf_blob_end
#ifndef ARM64_EMBEDDED_ELF_PATH
#define ARM64_EMBEDDED_ELF_PATH "build/embedded_elf_aarch64"
#endif
embedded_elf_blob:
	.incbin ARM64_EMBEDDED_ELF_PATH
embedded_elf_blob_end:
