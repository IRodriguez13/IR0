/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kexec.h
 * Description: Facade — kexec_load MVP (segment copy + reboot jump/magic).
 *
 * ABI: Linux kexec_load(2) subset (man7). No kexec_file_load / crash kernel.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define KEXEC_ON_CRASH		0x00000001ul
#define KEXEC_PRESERVE_CONTEXT	0x00000002ul
#define KEXEC_ARCH_MASK		0xffff0000ul
#define KEXEC_ARCH_DEFAULT	(0ul << 16)
#define KEXEC_ARCH_X86_64	(62ul << 16)
#define KEXEC_SEGMENT_MAX	4

struct kexec_segment
{
	void *buf;
	size_t bufsz;
	void *mem;
	size_t memsz;
};

/**
 * sys_kexec_load — copy up to KEXEC_SEGMENT_MAX segments into kernel RAM.
 * Returns 0 on success, negative errno otherwise.
 */
int64_t sys_kexec_load(unsigned long entry, unsigned long nr_segments,
		       struct kexec_segment *segments, unsigned long flags);

/** True if a successful kexec_load is staged. */
int ir0_kexec_image_loaded(void);

/**
 * Execute staged image: magic IR0KEXEC payload or jump to entry.
 * Does not return on success paths that reboot/halt.
 */
void ir0_kexec_execute(void);
