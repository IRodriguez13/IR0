/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_arch_portable.c
 * Description: ARM64 boot_arch hooks for the portable kernel (overrides weak boot_arch.c).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/boot_arch.h>
#include <ir0/boot_log.h>

#include <stdint.h>

#if !defined(IR0_FREESTANDING_BOOT)
#include "arm64_boot_common.h"
#endif

void ir0_boot_arch_firmware_early(uintptr_t firmware_handle)
{
#if !defined(IR0_FREESTANDING_BOOT)
	arm64_platform_setup_for_portable_boot(firmware_handle);
#else
	(void)firmware_handle;
#endif
}

void ir0_boot_arch_userspace_handoff_smoke(void)
{
#if defined(IR0_ARM64_PRODUCT_KERNEL)
	ir0_boot_smoke("ARM64_PRODUCT_BOOT_HANDOFF");
#endif
}
