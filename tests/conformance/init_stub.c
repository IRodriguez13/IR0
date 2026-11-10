/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: init_stub.c
 * Description: Minimal arm64 ELF for the /init slot of the test archive.
 *              The ARM64 early rootfs binds a CPIO only when /init is an ELF
 *              image, while the kernel execs /sbin/init; the 128 KiB early
 *              buffer cannot hold two copies of the conformance runner, so
 *              /init is this stub (it only exits) and /sbin/init the runner.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

__asm__(".text\n"
	".global _start\n"
	".type _start, %function\n"
	"_start:\n"
	"	mov	x0, #0\n"
	"	mov	x8, #93\n"
	"	svc	#0\n");
