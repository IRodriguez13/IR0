/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: boot.h
 * Description: ARM64 boot constants shared by entry.S and C.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/*
 * Stack used by kmain() until the first task switch. The common boot path
 * (KTM suite, MM and VFS init) needs more than 4 KiB; 16 KiB matches x86-64
 * (boot_x64.asm). Must stay a multiple of 4 KiB for the entry.S add.
 */
#define ARM64_BOOT_STACK_SIZE 0x4000

/*
 * The kernel runs from TTBR1, as Linux arm64 does: the image is linked at
 * KIMAGE_VADDR + KIMAGE_TEXT_OFFSET and mapped there, whatever 2 MiB
 * aligned physical base the loader chose. The mapping is KIMAGE_MAP_SIZE
 * long and holds the image and, KERNEL_HEAP_OFFSET into it, the early
 * kernel heap. arch/arm64/linker.ld repeats the link address.
 * Plain constants: entry.S includes this file too.
 */
#define KIMAGE_VADDR         0xffff800000000000
#define KIMAGE_TEXT_OFFSET   0x80000
#define KIMAGE_MAP_SIZE      0x2000000
#define KIMAGE_BLOCK_SIZE    0x200000
#define KERNEL_HEAP_OFFSET   0x800000
