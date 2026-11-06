/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_arch.c
 * Description: Default (weak) boot_arch hooks — strong definitions under arch/<isa>/.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/boot_arch.h>

#include <stdint.h>

__attribute__((weak)) void ir0_boot_arch_firmware_early(uintptr_t firmware_handle)
{
	(void)firmware_handle;
}

__attribute__((weak)) void ir0_boot_arch_userspace_handoff_smoke(void)
{
}
