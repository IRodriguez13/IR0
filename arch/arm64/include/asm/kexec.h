/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/*
 * Linux arch/arm64/include/asm/kexec.h: the architecture kexec_load(2)
 * accepts besides KEXEC_ARCH_DEFAULT. Selected by includes/ir0/kexec.h,
 * which defines the KEXEC_ARCH_* values.
 */
#define KEXEC_ARCH KEXEC_ARCH_AARCH64
