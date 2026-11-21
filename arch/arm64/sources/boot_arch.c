/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_arch.c
 * Description: ARM64 boot_arch hooks for the portable kernel
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/boot_arch.h>
#include <ir0/boot_log.h>
#include <ir0/driver_bootstrap.h>

#include <stdint.h>

#include "boot_common.h"

void ir0_boot_arch_firmware_early(const struct ir0_boot_params *params)
{
	arm64_platform_setup_for_portable_boot(params);
}

/*
 * QEMU virt and the Raspberry Pi boards have no bootstrap drivers: the
 * GIC, PL011, PL031, generic timer and virtio-mmio block device are brought
 * up by the early platform setup, and the bcmgenet NIC is listed in
 * drivers/net/net_drivers_dt.c.
 */
const struct driver_boot_table ir0_boot_arch_driver_table = { NULL, 0 };

void ir0_boot_arch_userspace_handoff_smoke(void)
{
#if defined(IR0_ARM64_PRODUCT_KERNEL)
	ir0_boot_smoke("ARM64_PRODUCT_BOOT_HANDOFF");
#endif
}
