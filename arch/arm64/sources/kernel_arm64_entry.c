/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel_arm64_entry.c
 * Description: ARM64 freestanding kmain — same entry contract as x86 (ir0_kernel_boot policy).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "arm64_boot_common.h"

#include <ir0/boot_flow.h>

#include <stdint.h>

void ir0_boot_freestanding(uintptr_t firmware_handle)
{
	arm64_early_bringup(firmware_handle);
}

void kmain(uintptr_t firmware_handle)
{
	ir0_kernel_boot(firmware_handle);
}
