/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_stub.c
 * Description: Legacy name — bring-up lives in arm64_boot_common.c; entry is kmain.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "arm64_boot_common.h"

#include <stdint.h>

extern volatile uintptr_t arm64_firmware_fdt;

void boot_main(void)
{
	arm64_early_bringup((uintptr_t)arm64_firmware_fdt);
}
