/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_arch.c
 * Description: ARM64 boot_arch hooks for the portable kernel (overrides weak boot_arch.c).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/boot_arch.h>
#include <ir0/boot_log.h>

#include <stdint.h>

#include "boot_common.h"

void ir0_boot_arch_firmware_early(const struct ir0_boot_params *params)
{
	arm64_platform_setup_for_portable_boot(params);
}

void ir0_boot_arch_userspace_handoff_smoke(void)
{
#if defined(IR0_ARM64_PRODUCT_KERNEL)
	ir0_boot_smoke("ARM64_PRODUCT_BOOT_HANDOFF");
#endif
}
